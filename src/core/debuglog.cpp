#include "core/debuglog.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace altair {
// Defined in board.cpp -- the one case-folding used for every operator-typed name.
std::string lowerAscii(std::string s);
} // namespace altair

namespace altair::dbg {

namespace {

// ---------------------------------------------------------------------------
// Process-global state. Function-local statics, so a static library Channel that
// constructs during static init (the 6850, the socket layer) finds a live registry
// no matter the translation-unit init order.
// ---------------------------------------------------------------------------

std::vector<Channel*>& registry() {
    static std::vector<Channel*> r;
    return r;
}

Sink&        sink()     { static Sink s = Sink::Stderr; return s; }
std::string& filePath() { static std::string p; return p; }

std::ofstream& fileStream() {
    static std::ofstream f;
    return f;
}

std::function<std::optional<uint16_t>()>& pcProvider() {
    static std::function<std::optional<uint16_t>()> p;
    return p;
}

// Split "sector, seek ,all" into folded, whitespace-trimmed tokens, dropping empties
// so a trailing comma is forgiving.
std::vector<std::string> tokens(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(csv);
    while (std::getline(ss, cur, ',')) {
        const auto b = cur.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const auto e = cur.find_last_not_of(" \t");
        out.push_back(lowerAscii(cur.substr(b, e - b + 1)));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------

Channel::Channel(std::string name, std::vector<std::string> flags)
    : name_(std::move(name)), flags_(std::move(flags)) {
    registry().push_back(this);
}

Channel::~Channel() {
    auto& r = registry();
    for (auto it = r.begin(); it != r.end(); ++it) {
        if (*it == this) { r.erase(it); break; }
    }
}

// Resolve a flag name to its bit index, or -1. Case already folded by tokens().
static int bitOf(const std::vector<std::string>& flags, const std::string& name) {
    for (size_t i = 0; i < flags.size(); ++i)
        if (flags[i] == name) return (int)i;
    return -1;
}

// The full-set mask for a channel with `n` flags: the low n bits.
static uint32_t allBits(size_t n) {
    return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
}

bool Channel::enable(const std::string& csv, std::string& err) {
    uint32_t next = mask_;  // additive: start from what is already on
    for (const std::string& t : tokens(csv)) {
        if (t == "all")       next = allBits(flags_.size());
        else if (t == "none") next = 0;
        else {
            int b = bitOf(flags_, t);
            if (b < 0) { err = "unknown debug flag '" + t + "' for " + name_; return false; }
            next |= (1u << b);
        }
    }
    mask_ = next;
    return true;
}

bool Channel::disable(const std::string& csv, std::string& err) {
    uint32_t next = mask_;
    for (const std::string& t : tokens(csv)) {
        if (t == "all")       next = 0;
        else if (t == "none") { /* no-op */ }
        else {
            int b = bitOf(flags_, t);
            if (b < 0) { err = "unknown debug flag '" + t + "' for " + name_; return false; }
            next &= ~(1u << b);
        }
    }
    mask_ = next;
    return true;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

std::vector<Channel*> channels() {
    return registry();
}

Channel* find(const std::string& name) {
    const std::string want = lowerAscii(name);
    for (Channel* c : registry())
        if (lowerAscii(c->name()) == want) return c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Sink
// ---------------------------------------------------------------------------

bool setSink(Sink s, const std::string& path, std::string& err) {
    if (s == Sink::File) {
        std::ofstream f(path, std::ios::out | std::ios::app | std::ios::binary);
        if (!f) { err = "cannot open debug file: " + path; return false; }
        fileStream() = std::move(f);
        filePath()   = path;
    } else {
        fileStream().close();
        fileStream().clear();
        filePath().clear();
    }
    sink() = s;
    return true;
}

std::string sinkName() {
    switch (sink()) {
        case Sink::Stdout: return "stdout";
        case Sink::File:   return filePath();
        case Sink::Stderr: break;
    }
    return "stderr";
}

std::ostream& out() {
    switch (sink()) {
        case Sink::Stdout: return std::cout;
        case Sink::File:   return fileStream();
        case Sink::Stderr: break;
    }
    return std::cerr;
}

// ---------------------------------------------------------------------------
// PC prefix + line
// ---------------------------------------------------------------------------

void setPcProvider(std::function<std::optional<uint16_t>()> p) {
    pcProvider() = std::move(p);
}

std::ostream& line(const Channel& ch) {
    std::ostream& os = out();

    std::optional<uint16_t> pc;
    if (auto& p = pcProvider()) pc = p();

    char buf[8];
    if (pc) std::snprintf(buf, sizeof buf, "%04X", (unsigned)*pc);
    else    std::snprintf(buf, sizeof buf, "----");

    os << buf << "  " << ch.name() << ": ";
    return os;
}

} // namespace altair::dbg
