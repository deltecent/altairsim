#include "host/diskfaults.h"

#include "core/statefile.h"

#include <cctype>
#include <sstream>

namespace altair {

namespace {

// The largest count a fault takes. Far past any driver's retry loop; it only keeps the
// number inside an int.
constexpr long kMaxCount = 1000000;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream       in(s);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// One field of the address: a decimal number in [0, max], or `*` (-1).
bool field(const std::string& s, int max, const char* what, int& out, std::string& err) {
    if (s == "*") {
        out = -1;
        return true;
    }
    if (s.empty() || s.size() > 6) {
        err = std::string("bad ") + what + " '" + s + "': give a decimal number or *";
        return false;
    }
    long v = 0;
    for (char c : s) {
        if (!std::isdigit((unsigned char)c)) {
            err = std::string("bad ") + what + " '" + s + "': give a decimal number or *";
            return false;
        }
        v = v * 10 + (c - '0');
    }
    if (v > max) {
        err = std::string(what) + " " + s + " is out of range (0.." + std::to_string(max) + ")";
        return false;
    }
    out = (int)v;
    return true;
}

bool address(const std::string& s, DiskFault& f, std::string& err) {
    const size_t a = s.find('/');
    const size_t b = a == std::string::npos ? a : s.find('/', a + 1);
    if (a == std::string::npos || b == std::string::npos || s.find('/', b + 1) != std::string::npos) {
        err = "bad sector address '" + s + "': give <track>/<head>/<sector>";
        return false;
    }
    return field(s.substr(0, a), 65535, "track", f.track, err) &&
           field(s.substr(a + 1, b - a - 1), 255, "head", f.head, err) &&
           field(s.substr(b + 1), 255, "sector", f.sector, err);
}

bool kindOf(const std::string& s, FaultKind& k, std::string& err) {
    const std::string w = lower(s);
    if (w == "data") k = FaultKind::Data;
    else if (w == "id") k = FaultKind::Id;
    else if (w == "missing") k = FaultKind::Missing;
    else if (w == "write") k = FaultKind::Write;
    else {
        err = "bad fault kind '" + s + "': give data, id, missing or write";
        return false;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

} // namespace

const char* faultKindName(FaultKind k) {
    switch (k) {
        case FaultKind::Data: return "data";
        case FaultKind::Id: return "id";
        case FaultKind::Missing: return "missing";
        case FaultKind::Write: return "write";
    }
    return "data";
}

std::string DiskFault::spec() const {
    auto f = [](int v) { return v < 0 ? std::string("*") : std::to_string(v); };
    std::string s = f(track) + "/" + f(head) + "/" + f(sector) + " " + faultKindName(kind);
    if (!hard()) s += " count=" + std::to_string(count);
    return s;
}

bool DiskFaults::parse(const std::string& spec, DiskFault& out, std::string& err) {
    const std::vector<std::string> w = words(spec);
    if (w.size() < 2 || w.size() > 3) {
        err = "bad fault '" + trim(spec) + "': give <track>/<head>/<sector> <kind> [count=N]";
        return false;
    }
    DiskFault f;
    if (!address(w[0], f, err) || !kindOf(w[1], f.kind, err)) return false;
    if (w.size() == 3) {
        const std::string c = lower(w[2]);
        if (c.rfind("count=", 0) != 0) {
            err = "bad fault option '" + w[2] + "': the only option is count=N";
            return false;
        }
        const std::string n = c.substr(6);
        long              v = 0;
        bool              okNum = !n.empty() && n.size() <= 7;
        for (char ch : n) okNum = okNum && std::isdigit((unsigned char)ch);
        if (okNum)
            for (char ch : n) v = v * 10 + (ch - '0');
        if (!okNum || v < 1 || v > kMaxCount) {
            err = "bad count '" + n + "': give 1.." + std::to_string(kMaxCount) +
                  ", or no count for a hard fault";
            return false;
        }
        f.count = (int)v;
        f.left  = (int)v;
    }
    out = f;
    return true;
}

bool DiskFaults::add(const std::string& spec, std::string& err) {
    DiskFault f;
    if (!parse(spec, f, err)) return false;
    add(f);
    return true;
}

void DiskFaults::add(const DiskFault& f) {
    for (DiskFault& e : list_) {
        if (e.sameAddress(f) && e.kind == f.kind) {
            e = f;
            return;
        }
    }
    list_.push_back(f);
}

bool DiskFaults::clear(const std::string& spec, std::string& err) {
    const std::vector<std::string> w = words(spec);
    if (w.empty() || w.size() > 2) {
        err = "bad fault '" + trim(spec) + "': give <track>/<head>/<sector> [<kind>]";
        return false;
    }
    DiskFault at;
    if (!address(w[0], at, err)) return false;
    bool      anyKind = w.size() == 1;
    FaultKind k       = FaultKind::Data;
    if (!anyKind && !kindOf(w[1], k, err)) return false;

    const size_t before = list_.size();
    std::vector<DiskFault> keep;
    for (const DiskFault& e : list_)
        if (!(e.sameAddress(at) && (anyKind || e.kind == k))) keep.push_back(e);
    if (keep.size() == before) {
        err = "no fault at " + trim(spec);
        return false;
    }
    list_ = std::move(keep);
    return true;
}

DiskFault* DiskFaults::find(int t, int h, int s, FaultKind k) {
    for (DiskFault& e : list_)
        if (e.kind == k && e.armed() && e.matches(t, h, s)) return &e;
    return nullptr;
}

const DiskFault* DiskFaults::find(int t, int h, int s, FaultKind k) const {
    for (const DiskFault& e : list_)
        if (e.kind == k && e.armed() && e.matches(t, h, s)) return &e;
    return nullptr;
}

void DiskFaults::use(DiskFault& f) {
    ++f.hits;
    if (!f.hard() && f.left > 0) --f.left;
}

std::string DiskFaults::toText() const {
    std::string s;
    for (const DiskFault& e : list_) {
        if (!s.empty()) s += "; ";
        s += e.spec();
    }
    return s;
}

bool DiskFaults::fromText(const std::string& text, std::string& err) {
    DiskFaults next;
    size_t     at = 0;
    while (at <= text.size()) {
        size_t semi = text.find(';', at);
        if (semi == std::string::npos) semi = text.size();
        const std::string one = trim(text.substr(at, semi - at));
        if (!one.empty() && !next.add(one, err)) return false;
        at = semi + 1;
    }
    list_ = std::move(next.list_);
    return true;
}

void DiskFaults::serialize(StateWriter& w) const {
    w.u32((uint32_t)list_.size());
    for (const DiskFault& e : list_) {
        w.str(e.spec());
        w.u32((uint32_t)e.left);
        w.u32((uint32_t)e.hits);
    }
}

void DiskFaults::deserialize(StateReader& r) {
    list_.clear();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        const std::string spec = r.str();
        const uint32_t    left = r.u32();
        const uint32_t    hits = r.u32();
        DiskFault         f;
        std::string       err;
        if (!parse(spec, f, err)) continue;  // a spec we wrote ourselves always parses
        f.left = f.hard() ? 0 : (int)left;
        f.hits = (int)hits;
        list_.push_back(f);
    }
}

} // namespace altair
