//
// THE VALIDATION GATE (DESIGN.md 3.2).
//
// Four period test suites, written to test REAL 8080s, run against ours. Until
// these pass, "the 8080 works" is an opinion -- and specifically it is MY
// opinion, formed from the same understanding that wrote the core, which means
// the unit tests in test_cpu.cpp share every one of its blind spots. These do
// not. They were written by people who had the chip on the bench and did not
// believe it either.
//
//   TST8080   Kelly Smith / Microcosm Associates, 1980. The classic smoke test.
//   8080PRE   Bartholomew & Cringle. A PRE-flight check for the exerciser below:
//             it tests the handful of instructions the exerciser itself needs,
//             so that a failure there is a plain English sentence instead of a
//             wrong CRC.
//   CPUTEST   SuperSoft Diagnostics II, 1981. Includes a TIMING test.
//   8080EXM   The exerciser. Runs every instruction against every interesting
//             operand and CRCs the result INCLUDING ALL FIVE FLAGS, then compares
//             against CRCs captured from real silicon. This is the one that
//             matters: it is the only thing here that can catch a half-carry that
//             is wrong only for one operand pair.
//
// HOW THE CP/M PROGRAMS RUN WITH NO CP/M AND NO CONSOLE CARD
//
// These are .COM files: they load at 0100 and call the BDOS at 0005. We have no
// disk, no CP/M, and (until milestone 1b) no 2SIO. So we supply the smallest
// thing that is not a lie:
//
//   - A BDOS stub AT F000, in real 8080 machine code, reached by the real JMP at
//     0005 that real CP/M puts there. It implements exactly functions 2 (write a
//     character) and 9 (write a $-terminated string) and does its output with a
//     real OUT to a real port on a real board in the backplane.
//   - A console card that decodes that one port.
//
// It would have been less code to trap PC==0005 in C++ and service the call
// there. That was rejected: it would mean the CALL, the RET, the stack it
// unwinds through, the LDAX D and the OUT were all being emulated by the
// harness, in the one program whose entire job is to decide whether we emulate
// them correctly. The stub costs 30 bytes and the CPU executes every one of them.
//
// The exit path is the same idea. Location 0000 is CP/M's warm boot; we put a
// HLT there, so a program that finishes by jumping to 0000 stops the machine
// through the CPU's own HLT path. And ALL the rest of page zero is filled with
// HLT too -- so a stray RST into an unset vector stops dead and gets reported,
// instead of running through a field of NOPs into the program and producing a
// mysterious wrong answer half an hour later.
//

#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "core/machine.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t  kConsolePort = 0x01;
constexpr uint16_t kBdos        = 0xF000;  // stub entry; also the top of the TPA
constexpr uint16_t kTpa         = 0x0100;  // where a .COM loads. Always.

// ---------------------------------------------------------------------------
// The console card. One write-only port and a string behind it.
//
// This is NOT a 2SIO and must never grow into one -- the real serial card is
// milestone 1b and it has a status register, a baud strap and an interrupt
// jumper, none of which belong here. This is a character sink on the backplane,
// which is all the harness needs and all it is allowed to have.
// ---------------------------------------------------------------------------
class ConsoleBoard : public Board {
public:
    std::string out;

    std::string type() const override { return "harness-console"; }

    bool decodes(const BusCycle& c) const override {
        return c.type == Cycle::IoWrite && c.port() == kConsolePort;
    }

    void write(const BusCycle& c) override {
        char ch = (char)c.data;
        out += ch;
        // Live, so a suite that takes two minutes is visibly alive rather than
        // apparently hung. CPUTEST prints "ABCDEF..." as it goes for exactly
        // this reason; it would be a shame to buffer it away.
        std::fputc(ch, stdout);
        std::fflush(stdout);
    }

    std::vector<Property> properties() override { return {}; }
};

// ---------------------------------------------------------------------------
// The BDOS stub, hand-assembled. Reads down the left column as 8080 source
// because that is what it is.
// ---------------------------------------------------------------------------
const uint8_t kBdosStub[] = {
    // F000  BDOS:
    0x79,                    //         MOV  A,C        ; function number
    0xFE, 0x02,              //         CPI  2          ; conout?
    0xCA, 0x0E, 0xF0,        //         JZ   CONOUT
    0xFE, 0x09,              //         CPI  9          ; print string?
    0xCA, 0x13, 0xF0,        //         JZ   PRSTR
    0xAF,                    //         XRA  A          ; anything else: return 0
    0xC9,                    //         RET             ;   and don't pretend.
    0x00,                    //  (pad)
    // F00E  CONOUT:
    0x7B,                    //         MOV  A,E
    0xD3, kConsolePort,      //         OUT  1
    0xC9,                    //         RET
    0x00,                    //  (pad)
    // F013  PRSTR:
    0x1A,                    //         LDAX D
    0xFE, 0x24,              //         CPI  '$'
    0xC8,                    //         RZ
    0xD3, kConsolePort,      //         OUT  1
    0x13,                    //         INX  D
    0xC3, 0x13, 0xF0,        //         JMP  PRSTR
};

struct Suite {
    const char* file;
    const char* pass;      // must appear
    const char* fail;      // must NOT appear (empty: no failure banner exists)
    uint64_t    maxInsns;  // runaway guard
};

// The strings are the suites' OWN, read out of their own source and .COM images
// -- not invented here. 8080PRE has no failure banner at all: it either prints
// its one completion line or it does not, so for it "pass" is the only signal
// and silence IS the failure.
const Suite kSuites[] = {
    {"TST8080.COM", "CPU IS OPERATIONAL",              "CPU HAS FAILED",         50'000'000ull},
    {"8080PRE.COM", "8080 Preliminary tests complete", "",                       50'000'000ull},
    {"CPUTEST.COM", "CPU TESTS OK",                    "ERROR",           1'000'000'000ull},
    {"8080EXM.COM", "Tests complete",                  "ERROR ****",     10'000'000'000ull},
};

struct Rig {
    Machine       m;
    ConsoleBoard* con = nullptr;
    CpuCore*      cpu = nullptr;

    Rig() {
        std::string err;

        Board* b = m.add("memory", "mem0", err);
        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;  // 64K. A .COM expects the whole address space to be there.
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();

        auto owned = std::make_unique<ConsoleBoard>();
        con = owned.get();
        con->id = "con0";
        m.bus.attach(con);
        console_ = std::move(owned);

        m.add("8080", "cpu0", err);
        cpu = m.cpu();
        cpu->reset(Reset::PowerOn);
    }

    ~Rig() { m.bus.detach(con); }

    void poke(uint16_t at, const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) m.bus.memWrite((uint16_t)(at + i), p[i]);
    }

    void setReg(const char* name, uint32_t v) {
        for (const RegDef& rd : cpu->registers())
            if (rd.name == name) rd.set(v);
    }

private:
    std::unique_ptr<ConsoleBoard> console_;
};

bool loadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[4096];
    size_t  n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

bool run(const Suite& s, const std::string& dir) {
    std::printf("\n---- %s ----------------------------------------------------\n", s.file);

    std::vector<uint8_t> image;
    if (!loadFile(dir + "/" + s.file, image)) {
        std::printf("  FAIL  cannot read %s/%s\n", dir.c_str(), s.file);
        return false;
    }
    if (kTpa + image.size() > kBdos) {
        std::printf("  FAIL  %s is %zu bytes -- it would overwrite the BDOS stub\n", s.file,
                    image.size());
        return false;
    }

    Rig rig;

    // Page zero is a minefield ON PURPOSE. See the header comment.
    for (uint16_t a = 0; a < 0x0100; ++a) rig.m.bus.memWrite(a, 0x76);  // HLT

    const uint8_t warm[] = {0xC3, (uint8_t)(kBdos & 0xFF), (uint8_t)(kBdos >> 8)};
    rig.poke(0x0005, warm, sizeof warm);  // 0005: JMP F000, and 0006 = top of TPA
    rig.m.bus.memWrite(0x0000, 0x76);     // 0000: HLT -- warm boot stops the machine

    rig.poke(kBdos, kBdosStub, sizeof kBdosStub);
    rig.poke(kTpa, image.data(), image.size());

    // CP/M hands a program a stack with the warm-boot address already on it, so
    // a program that finishes with a plain RET goes to 0000 -- and 0000 is a HLT.
    // Both exit conventions therefore land in the same place.
    rig.m.bus.memWrite(0xEFFE, 0x00);
    rig.m.bus.memWrite(0xEFFF, 0x00);
    rig.setReg("SP", 0xEFFE);
    rig.cpu->setPc(kTpa);

    BusMaster* master = rig.m.master();
    auto       t0     = std::chrono::steady_clock::now();

    uint64_t insns = 0, tStates = 0;
    bool     halted = false;
    while (insns < s.maxInsns) {
        StepResult r = master->step(rig.m.bus);
        ++insns;
        tStates += r.tStates;

        // Nothing in THIS harness reads the clock -- the console card is a plain
        // character sink with no timing on it. It is advanced anyway, because
        // "emulated time moves when the CPU retires an instruction" is either true
        // everywhere or it is not a rule, and the day somebody puts a real 2SIO in
        // here to test an interrupt-driven suite, it must already be true.
        rig.m.clock.advance(r.tStates);

        if (r.status == RunStatus::Halted) {
            halted = true;
            break;
        }
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // The HLT we executed is at pc-1. Exit through 0000 is the good one; a HLT
    // anywhere else in page zero is a stray RST landing in an unset vector, and a
    // HLT outside page zero is the program itself giving up.
    uint16_t hltAt = (uint16_t)(rig.cpu->pc() - 1);

    std::printf("\n");
    std::printf("  %llu instructions, %llu T-states (%.1fs simulated at 2 MHz) in %.1fs real"
                " -- %.1f MHz effective\n",
                (unsigned long long)insns, (unsigned long long)tStates,
                (double)tStates / 2e6, secs, (double)tStates / secs / 1e6);

    const std::string& out = rig.con->out;
    bool ok = true;

    if (!halted) {
        std::printf("  FAIL  did not terminate within %llu instructions\n",
                    (unsigned long long)s.maxInsns);
        ok = false;
    } else if (hltAt != 0x0000) {
        std::printf("  FAIL  unexpected HLT at %04X%s\n", hltAt,
                    hltAt < 0x0100 ? " -- a stray RST into an unset page-zero vector" : "");
        ok = false;
    }

    if (out.find(s.pass) == std::string::npos) {
        std::printf("  FAIL  never printed \"%s\"\n", s.pass);
        ok = false;
    }
    if (*s.fail && out.find(s.fail) != std::string::npos) {
        std::printf("  FAIL  printed \"%s\"\n", s.fail);
        ok = false;
    }

    if (ok) std::printf("  PASS  %s\n", s.file);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = ALTAIR_CPU_TEST_DIR;

    std::printf("8080 VALIDATION GATE (DESIGN.md 3.2) -- suites from %s\n", dir.c_str());

    int failed = 0, ran = 0;
    for (const Suite& s : kSuites) {
        // With no arguments, run them all. With arguments, run the named ones --
        // 8080EXM is minutes long and you do not want it on every save.
        if (argc > 1) {
            bool wanted = false;
            for (int i = 1; i < argc; ++i)
                if (strncasecmp(argv[i], s.file, strlen(argv[i])) == 0) wanted = true;
            if (!wanted) continue;
        }
        ++ran;
        if (!run(s, dir)) ++failed;
    }

    std::printf("\n==========================================================\n");
    if (!ran) {
        std::printf("no suite matched. known: TST8080 8080PRE CPUTEST 8080EXM\n");
        return 2;
    }
    std::printf("%d suite%s run, %d failed\n", ran, ran == 1 ? "" : "s", failed);
    return failed ? 1 : 0;
}
