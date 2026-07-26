#include "test.h"

#include "host/endpoint.h"
#include "host/printer_stream.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace altair;

namespace {

// The whole point of the boundary tests: a FAKE submit sink and a FAKE wall clock, so
// they feed bytes and advance host seconds with no CUPS and no paper (docs/printing.md
// 4). The sink captures each job as it is submitted; the clock only moves when the
// test moves it, the way the VDM-1 blink test drives DisplayNull.
struct FakeSink {
    std::vector<std::vector<uint8_t>> jobs;
    bool                              failNext = false;
    std::string                       failMsg  = "printer offline";

    PrinterStream::Submit fn() {
        return [this](const std::vector<uint8_t>& d, std::string& e) {
            if (failNext) {
                e = failMsg;
                return false;
            }
            jobs.push_back(d);
            return true;
        };
    }
};

struct FakeClock {
    uint64_t                  ns = 0;
    std::function<uint64_t()> fn() {
        return [this] { return ns; };
    }
    void advance(double seconds) { ns += (uint64_t)(seconds * 1'000'000'000.0); }
};

std::vector<uint8_t> vec(const std::string& s) { return {s.begin(), s.end()}; }

void put(PrinterStream& ps, const std::string& s) {
    ps.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

const uint64_t kBig = 16ull * 1024 * 1024;  // the runaway ceiling, out of the way here

} // namespace

void test_printer() {
    SECTION("printer: idle timer submits exactly one job of the right bytes");
    {
        FakeSink  sink;
        FakeClock clk;
        {
            PrinterStream ps("printer:lw?idle=5", {5, false, kBig}, sink.fn(), clk.fn());
            put(ps, "HELLO");
            ps.pump();
            CHECK(sink.jobs.empty(), "nothing goes out before the idle time elapses");
            clk.advance(4.9);
            ps.pump();
            CHECK(sink.jobs.empty(), "and nothing goes out a hair early");
            clk.advance(0.2);  // now 5.1s of silence
            ps.pump();
            CHECK(sink.jobs.size() == 1, "one job after the idle time elapses");
            if (!sink.jobs.empty()) CHECK(sink.jobs[0] == vec("HELLO"), "with the bytes the guest wrote");
        }
        CHECK(sink.jobs.size() == 1, "the destructor does not add an empty second job");
    }

    SECTION("printer: an empty buffer never submits");
    {
        FakeSink  sink;
        FakeClock clk;
        {
            PrinterStream ps("printer:lw?idle=5", {5, false, kBig}, sink.fn(), clk.fn());
            clk.advance(100);  // a long silence, but nothing was ever written
            ps.pump();
            CHECK(sink.jobs.empty(), "an idle timer with nothing buffered submits nothing");
        }
        CHECK(sink.jobs.empty(), "and neither does the destructor");
    }

    SECTION("printer: a form feed closes the page, including the 0x0C, and never a blank");
    {
        // idle=0 (never) isolates the form-feed boundary from the timer.
        FakeSink  sink;
        FakeClock clk;
        {
            PrinterStream ps("printer:lw?onff&idle=0", {0, true, kBig}, sink.fn(), clk.fn());
            std::string   two = "PAGE1";
            two += (char)0x0C;
            two += "PAGE2";
            put(ps, two);
            CHECK(sink.jobs.size() == 1, "the form feed closed exactly one job");
            std::string page1 = "PAGE1";
            page1 += (char)0x0C;
            if (!sink.jobs.empty()) CHECK(sink.jobs[0] == vec(page1), "the closed page carries the form feed byte");
        }
        CHECK(sink.jobs.size() == 2, "the destructor flushes the trailing page");
        if (sink.jobs.size() == 2) CHECK(sink.jobs[1] == vec("PAGE2"), "which is exactly the trailing bytes");
    }

    SECTION("printer: a trailing form feed leaves no blank job behind");
    {
        FakeSink  sink;
        FakeClock clk;
        {
            PrinterStream ps("printer:lw?onff&idle=0", {0, true, kBig}, sink.fn(), clk.fn());
            std::string   rpt = "RPT";
            rpt += (char)0x0C;  // form feed is the very last byte
            put(ps, rpt);
            CHECK(sink.jobs.size() == 1, "the form feed closes the report page");
        }
        CHECK(sink.jobs.size() == 1, "and the now-empty buffer submits no blank page at teardown");
    }

    SECTION("printer: the max ceiling closes a job and the remainder is flushed at teardown");
    {
        FakeSink  sink;
        FakeClock clk;
        {
            PrinterStream ps("printer:lw?max=4&idle=0", {0, false, 4}, sink.fn(), clk.fn());
            put(ps, "ABCDEFG");
            CHECK(sink.jobs.size() == 1, "the byte ceiling closed a job at 4 bytes");
            if (!sink.jobs.empty()) CHECK(sink.jobs[0] == vec("ABCD"), "the job is the first max bytes");
        }
        CHECK(sink.jobs.size() == 2, "the remainder becomes its own job at teardown");
        if (sink.jobs.size() == 2) CHECK(sink.jobs[1] == vec("EFG"), "and it is the trailing bytes");
    }

    SECTION("printer: the destructor is the safety net for a held job");
    {
        FakeSink sink;
        {
            // Default steady clock -- never advanced, so the idle timer cannot fire;
            // only the destructor can push this out. This is the DISCONNECT / exit case.
            PrinterStream ps("printer:lw", {5, false, kBig}, sink.fn());
            put(ps, "TAIL");
            CHECK(sink.jobs.empty(), "held while the machine runs");
        }
        CHECK(sink.jobs.size() == 1, "and submitted when the stream is destroyed");
        if (!sink.jobs.empty()) CHECK(sink.jobs[0] == vec("TAIL"), "with the held bytes");
    }

    SECTION("printer: a failed submit reaches the operator through drainLog and drops the job");
    {
        FakeSink sink;
        sink.failNext = true;
        FakeClock clk;
        PrinterStream ps("printer:lw?idle=1", {1, false, kBig}, sink.fn(), clk.fn());
        put(ps, "X");
        clk.advance(1.5);
        ps.pump();
        auto log = ps.drainLog();
        CHECK(log.size() == 1, "a failed submit records one message");
        if (!log.empty()) CHECK(log[0].find("printer offline") != std::string::npos, "and it is the sink's reason");
        CHECK(sink.jobs.empty(), "a failed job captured nothing");
        // The doomed buffer was cleared, so nothing is retried when ps is destroyed --
        // and drainLog() moved the message out, so a second drain is empty.
        CHECK(ps.drainLog().empty(), "drainLog clears what it returned");
    }

#ifdef ALTAIRSIM_ENABLE_PRINTER
    // These go through the REAL resolver, so they exercise the grammar an operator's
    // CONNECT does. They write NO bytes, so every stream destructs with an empty buffer
    // and no real print job is ever submitted to a real queue.
    SECTION("printer: the endpoint grammar parses and round-trips through describe()");
    {
        std::string err;
        auto        s = resolveEndpoint("printer:linewriter?idle=15&onff", err);
        CHECK(s != nullptr, "printer:QUEUE?opts resolves to a stream");
        if (s) {
            CHECK(s->describe() == "printer:linewriter?idle=15&onff",
                  "describe() returns the exact spec (SHOW / CONFIG SAVE round-trip)");
            CHECK(s->writable(), "a printer is always writable");
            CHECK(!s->readable(), "and never readable -- nothing comes back off paper");
        }
    }

    SECTION("printer: an unknown option is refused and the error names the real ones");
    {
        std::string err;
        auto        bad = resolveEndpoint("printer:lw?idel=10", err);
        CHECK(bad == nullptr, "a misspelled option does not silently resolve");
        CHECK(err.find("idle") != std::string::npos && err.find("onff") != std::string::npos &&
                  err.find("max") != std::string::npos,
              "the error names idle, onff and max");
    }

    SECTION("printer: a queue name may contain spaces and colons");
    {
        std::string err;
        auto        s = resolveEndpoint("printer:Generic / Text Only?idle=0", err);
        CHECK(s != nullptr, "a spaced queue name parses (the operator quotes it at the prompt)");
        if (s)
            CHECK(s->describe() == "printer:Generic / Text Only?idle=0",
                  "and the whole spec round-trips");
    }

    SECTION("printer: an empty queue name is refused");
    {
        std::string err;
        auto        s = resolveEndpoint("printer:", err);
        CHECK(s == nullptr, "printer: with no queue is an error, not a silent null line");
    }
#endif
}
