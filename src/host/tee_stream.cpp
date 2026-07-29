#include "host/tee_stream.h"

#include "platform/localtime.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace altair {
namespace {

// The default wall source: a monotonic host clock in nanoseconds, the same one the
// printer's idle timer and the tape reader use. steady_clock never runs backwards, so
// an idle-gap flush can never fire early because someone stepped NTP.
uint64_t steadyNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr uint64_t kNsPerSecond = 1'000'000'000ull;

// One byte as two uppercase hex nibbles -- the on-the-wire convention (value.h: on the
// wire is hex). Model on the inline %02X in the MCP dump (src/mcp/server.cpp).
void appendHex(std::string& s, uint8_t b) {
    static const char* d = "0123456789ABCDEF";
    s += d[b >> 4];
    s += d[b & 0xF];
}

// A byte as its printable glyph, or '.' for anything a terminal would not show. The
// ASCII gutter of a hexdump, the half a human actually reads.
char printable(uint8_t b) { return (b >= 0x20 && b < 0x7F) ? (char)b : '.'; }

// JSON string escaping for the jsonl format's `hex`/`txt`/`capture` fields. Minimal but
// correct: the control codes and the two structural characters, so a `"` or a `\` in a
// captured line cannot break the record it lands in.
std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// "YYYY-MM-DD HH:MM:SS" in the host's local zone -- the capture header's date.
std::string calendarLine(std::time_t t) {
    platform::CalendarTime c = platform::localCalendar(t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day,
                  c.hour, c.minute, c.second);
    return buf;
}

const char* fmtName(TeeStream::Fmt f) {
    switch (f) {
        case TeeStream::Fmt::Dump: return "dump";
        case TeeStream::Fmt::Cols: return "cols";
        case TeeStream::Fmt::Jsonl: return "jsonl";
    }
    return "dump";
}

// The two column widths the dump/cols layouts share -- kept here so the `cols` legend
// header and its data rows cannot drift out of alignment.
constexpr int kTimeCol = 13;  // width of the leading timestamp column (0 when ts=none)
int    timeColWidth(TeeStream::Ts ts) { return ts == TeeStream::Ts::None ? 0 : kTimeCol; }
size_t regionWidth(int width) { return (size_t)width * 3 + width + 4; }  // hex + ascii + slack

} // namespace

TeeStream::TeeStream(std::unique_ptr<ByteStream> inner, std::string fileSpec, Params p,
                     std::ofstream log, std::function<uint64_t()> hostNs, std::time_t wallBase)
    : inner_(std::move(inner)), fileSpec_(std::move(fileSpec)), p_(p),
      hostNs_(hostNs ? std::move(hostNs) : steadyNs), log_(std::move(log)) {
    wallBaseSecs_ = wallBase ? wallBase : std::time(nullptr);
    wallBaseNs_   = hostNs_();

    // The header. dump/cols get a `#` comment banner (and cols a column legend);
    // jsonl gets a JSON meta record so the file stays valid JSONL end to end.
    if (p_.fmt == Fmt::Jsonl) {
        std::string s = "{\"capture\":\"" + jsonEscape(inner_->describe()) + "\",\"started\":\"" +
                        calendarLine(wallBaseSecs_) + "\",\"fmt\":\"jsonl\"}";
        writeLine(s);
    } else {
        writeLine("# altairsim capture  " + inner_->describe() + "  " +
                  calendarLine(wallBaseSecs_) + "  fmt=" + fmtName(p_.fmt));
        if (p_.fmt == Fmt::Cols) {
            int         tc  = timeColWidth(p_.ts);
            size_t      reg = regionWidth(p_.width);
            std::string legend;
            if (tc) {
                legend = "time";
                legend.resize(tc, ' ');
            }
            legend += "TX (guest -> line)";
            legend.resize(tc + reg, ' ');  // the RX column starts where a TX row's does
            legend += "RX (line -> guest)";
            writeLine(legend);
        }
    }
    log_.flush();
}

TeeStream::~TeeStream() {
    // The safety net: whatever partial row the guest left becomes one last line here,
    // the one point DISCONNECT / CONFIG LOAD / exit all reach. Never throw out of a
    // destructor during teardown.
    try {
        endBurst();
        log_.flush();
    } catch (...) {
        // never propagate
    }
}

size_t TeeStream::write(const uint8_t* buf, size_t n) {
    if (n) emitData(true, buf, n);
    return inner_->write(buf, n);
}

size_t TeeStream::read(uint8_t* buf, size_t n) {
    size_t got = inner_->read(buf, n);
    if (got) emitData(false, buf, got);  // only what actually arrived, never an empty poll
    return got;
}

void TeeStream::flush() {
    inner_->flush();
    log_.flush();  // NOT a row boundary -- just make the capture live for tail -f
}

void TeeStream::pump() {
    inner_->pump();
    // End a burst that has gone quiet, so a request that got no reply still lands on
    // disk instead of hanging in the buffer -- the printer's idle-timer discipline.
    if (burstActive_ && p_.gapNs && now() - lastByteNs_ >= p_.gapNs) endBurst();
}

// ---- the far end's pins, logged on the edge and forwarded untouched ----

void TeeStream::setControl(const LineControl& c) {
    if (p_.pins) {
        if (c.rts != prevControl_.rts) emitPin("RTS", c.rts);
        if (c.dtr != prevControl_.dtr) emitPin("DTR", c.dtr);
        if (c.brk != prevControl_.brk) emitPin("BRK", c.brk);
    }
    prevControl_ = c;
    inner_->setControl(c);
}

LineStatus TeeStream::status() const {
    LineStatus s = inner_->status();
    if (p_.pins) {
        if (!statusInit_) {
            prevStatus_ = s;
            statusInit_ = true;
        } else {
            if (s.carrier != prevStatus_.carrier) emitPin("DCD", s.carrier);
            if (s.cts != prevStatus_.cts) emitPin("CTS", s.cts);
            if (s.dsr != prevStatus_.dsr) emitPin("DSR", s.dsr);
            if (s.ring != prevStatus_.ring) emitPin("RI", s.ring);
            prevStatus_ = s;
        }
    }
    return s;
}

bool TeeStream::setParams(const LineParams& p, std::string& err) {
    if (p_.pins) {
        bool changed = !paramsInit_ || p.baud != prevParams_.baud ||
                       p.dataBits != prevParams_.dataBits || p.stopBits != prevParams_.stopBits ||
                       p.parity != prevParams_.parity;
        if (changed) {
            const char par = p.parity == LineParity::Even ? 'E'
                             : p.parity == LineParity::Odd ? 'O'
                                                           : 'N';
            char buf[48];
            std::snprintf(buf, sizeof(buf), "[LINE %lld %d%c%d]", p.baud, p.dataBits, par,
                          p.stopBits);
            endBurst();
            writeLine(stamp() + buf);
            prevParams_ = p;
            paramsInit_ = true;
        }
    }
    return inner_->setParams(p, err);
}

std::vector<std::string> TeeStream::drainLog() {
    auto out = inner_->drainLog();
    for (auto& m : log_msgs_) out.push_back(std::move(m));
    log_msgs_.clear();
    return out;
}

// ---- formatting ----

std::string TeeStream::stamp() const {
    if (p_.ts == Ts::None) return "";
    uint64_t t = now();
    if (!baseSet_) {
        baseNs_  = t;
        baseSet_ = true;
    }
    char buf[40];
    if (p_.ts == Ts::Wall) {
        // Calendar time = the seed captured at construction + however long has elapsed
        // on the monotonic clock since; the fraction comes from that delta.
        uint64_t              deltaNs = t - wallBaseNs_;
        std::time_t           secs    = wallBaseSecs_ + (std::time_t)(deltaNs / kNsPerSecond);
        platform::CalendarTime c       = platform::localCalendar(secs);
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06llu  ", c.hour, c.minute, c.second,
                      (unsigned long long)(deltaNs % kNsPerSecond) / 1000);
    } else {
        uint64_t elapsed = t - baseNs_;
        std::snprintf(buf, sizeof(buf), "+%llu.%06llu  ", (unsigned long long)(elapsed / kNsPerSecond),
                      (unsigned long long)(elapsed % kNsPerSecond) / 1000);
    }
    return buf;
}

void TeeStream::emitData(bool tx, const uint8_t* buf, size_t n) const {
    // jsonl is one record per transfer -- no row grouping at all.
    if (p_.fmt == Fmt::Jsonl) {
        std::string hex, txt;
        for (size_t i = 0; i < n; ++i) {
            appendHex(hex, buf[i]);
            txt += printable(buf[i]);
        }
        uint64_t t = now();
        if (!baseSet_) {
            baseNs_  = t;
            baseSet_ = true;
        }
        std::string rec = "{\"ns\":" + std::to_string(t - baseNs_) + ",\"dir\":\"" +
                          (tx ? "tx" : "rx") + "\",\"hex\":\"" + hex + "\",\"txt\":\"" +
                          jsonEscape(txt) + "\"}";
        writeLine(rec);
        lastByteNs_ = t;
        return;
    }

    // dump/cols: accumulate same-direction bytes into `width`-byte rows. A direction
    // change ends the current burst first, so the two directions never share a row.
    uint64_t t = now();
    for (size_t i = 0; i < n; ++i) {
        if (burstActive_ && burstTx_ != tx) endBurst();
        if (!burstActive_) {
            burstActive_ = true;
            burstTx_     = tx;
            burstOffset_ = 0;
        }
        if (row_.empty()) rowStartNs_ = t;
        row_.push_back(buf[i]);
        if ((int)row_.size() >= p_.width) flushRow();  // a full row; the burst continues
    }
    lastByteNs_ = t;
}

void TeeStream::flushRow() const {
    if (row_.empty()) return;

    // The timestamp for the row is when its FIRST byte landed (stamp() would read the
    // current time, so a row is formatted here directly off rowStartNs_).
    std::string ts;
    if (p_.ts != Ts::None) {
        if (!baseSet_) {
            baseNs_  = rowStartNs_;
            baseSet_ = true;
        }
        char buf[40];
        if (p_.ts == Ts::Wall) {
            uint64_t               deltaNs = rowStartNs_ - wallBaseNs_;
            std::time_t            secs    = wallBaseSecs_ + (std::time_t)(deltaNs / kNsPerSecond);
            platform::CalendarTime c       = platform::localCalendar(secs);
            char                   tb[40];
            std::snprintf(tb, sizeof(tb), "%02d:%02d:%02d.%06llu", c.hour, c.minute, c.second,
                          (unsigned long long)(deltaNs % kNsPerSecond) / 1000);
            ts = tb;
        } else {
            uint64_t elapsed = rowStartNs_ - baseNs_;
            std::snprintf(buf, sizeof(buf), "+%llu.%06llu", (unsigned long long)(elapsed / kNsPerSecond),
                          (unsigned long long)(elapsed % kNsPerSecond) / 1000);
            ts = buf;
        }
    }

    // The hex + ascii region for this row's bytes.
    std::string hex, ascii;
    for (size_t i = 0; i < row_.size(); ++i) {
        appendHex(hex, row_[i]);
        hex += ' ';
        ascii += printable(row_[i]);
    }

    std::string line;
    if (p_.fmt == Fmt::Cols) {
        // TX fills the left region, RX is indented to the right one (past a whole TX
        // region), so a conversation reads down the page. Same widths as the legend.
        int         tc   = timeColWidth(p_.ts);
        std::string cell = hex + " " + ascii;
        if (tc) {
            line = ts;
            line.resize(tc, ' ');
        }
        if (burstTx_) {
            line += cell;
        } else {
            line.resize(line.size() + regionWidth(p_.width), ' ');
            line += cell;
        }
    } else {
        // dump: a labeled, offset hex row -- the protocol-analyzer line.
        if (int tc = timeColWidth(p_.ts)) {
            line = ts;
            line.resize(tc, ' ');
        }
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%s %04zX  ", burstTx_ ? "TX" : "RX", burstOffset_);
        line += lbl;
        size_t hexField = (size_t)p_.width * 3;
        std::string h   = hex;
        h.resize(hexField, ' ');
        line += h + " " + ascii;
    }
    writeLine(line);

    burstOffset_ += row_.size();
    row_.clear();
}

void TeeStream::endBurst() const {
    flushRow();
    burstActive_ = false;
    burstOffset_ = 0;
}

void TeeStream::emitPin(const char* pin, bool up) const {
    if (p_.fmt == Fmt::Jsonl) {
        uint64_t t = now();
        if (!baseSet_) {
            baseNs_  = t;
            baseSet_ = true;
        }
        std::string rec = "{\"ns\":" + std::to_string(t - baseNs_) + ",\"pin\":\"" + pin +
                          "\",\"edge\":\"" + (up ? "up" : "down") + "\"}";
        writeLine(rec);
        return;
    }
    // A pin edge is its own event -- flush any pending data first so the trace stays in
    // time order. `^` is a rising edge (asserted), `_` a falling one.
    endBurst();
    std::string s = stamp();
    s += "[";
    s += pin;
    s += up ? "^]" : "_]";
    writeLine(s);
}

void TeeStream::writeLine(const std::string& s) const {
    if (!log_) return;
    log_ << s << '\n';
    if (!log_) log_msgs_.push_back("capture: write to log file failed");
}

} // namespace altair
