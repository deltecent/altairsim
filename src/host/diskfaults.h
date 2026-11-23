#pragma once
//
// DiskFaults -- the bad sectors of one mounted diskette, as an OVERLAY (#966).
//
// A raw .DSK holds sector payloads and nothing else (DESIGN.md 7.3): no address marks, no
// CRCs, and so no way to be a bad sector. That leaves a driver's retry code with nothing to
// retry. This table is the bad spots of the diskette in a drive, kept BESIDE the image and
// never written into it -- the image bytes do not change. The drive (DiskImageDrive) asks it
// whether a sector is bad as the chip reads the ID fields and the data; the chip then reports
// the error the way the silicon does.
//
// THE SPEC, shared by FAULT, the MCP tools and the machine file:
//
//     <track>/<head>/<sector> <kind> [count=N]
//
//   - track, head and sector are decimal, or `*` for any. The sector is the ID the guest asks
//     for (the recorded ID, or startSector + index on a track with no recorded list), so the
//     0-based / 1-based numbering of a controller never moves a fault.
//   - kind: data (bad data-field CRC; the data still transfers), id (bad ID-field CRC),
//     missing (no ID field at all), write (the drive will not take a write).
//   - No count is a HARD fault: it never wears off. count=N fails N times, then the sector
//     reads clean -- which is how a test proves that a retry recovers.
//
// `hits` counts every time the fault was used, hard or not, so a test can check how many
// times a driver tried.

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class StateWriter;
class StateReader;

enum class FaultKind { Data, Id, Missing, Write };

const char* faultKindName(FaultKind k);

struct DiskFault {
    int       track  = -1;  // -1 = any
    int       head   = -1;
    int       sector = -1;
    FaultKind kind   = FaultKind::Data;
    int       count  = 0;   // 0 = hard
    int       left   = 0;   // uses before it wears off (count faults only)
    int       hits   = 0;   // times used

    bool hard() const { return count == 0; }
    bool armed() const { return hard() || left > 0; }
    bool matches(int t, int h, int s) const {
        return (track < 0 || track == t) && (head < 0 || head == h) && (sector < 0 || sector == s);
    }
    bool sameAddress(const DiskFault& o) const {
        return track == o.track && head == o.head && sector == o.sector;
    }
    std::string spec() const;  // "5/0/3 data count=2" -- parse() reads it back
};

class DiskFaults {
public:
    // Parse one spec. False with `err` set on a bad one.
    static bool parse(const std::string& spec, DiskFault& out, std::string& err);

    // Add a fault. One with the same address and kind is REPLACED, counters and all. A kind the
    // drive has refused (refuse()) fails with the reason.
    bool add(const std::string& spec, std::string& err);
    void add(const DiskFault& f);

    // Remove the faults at this address ("5/0/3", or "5/0/3 data" for one kind). The address
    // matches as written: `5/*/*` removes the `5/*/*` entry, not every fault on track 5.
    // False with `err` set if the spec is bad or names no fault.
    bool clear(const std::string& spec, std::string& err);
    void clearAll() { list_.clear(); }

    // A kind this drive's medium cannot have, and why. A North Star sector has no ID field, so
    // its drives refuse `id` (#970). add() and fromText() then fail with `why`. clearAll() and
    // deserialize() keep the refusals: they belong to the drive, not to the diskette.
    void refuse(FaultKind k, std::string why) { refused_[(int)k] = std::move(why); }

    // The first ARMED fault of this kind that covers the sector, or null.
    DiskFault*       find(int t, int h, int s, FaultKind k);
    const DiskFault* find(int t, int h, int s, FaultKind k) const;

    // Use one: count the hit, and wear a count fault down by one.
    static void use(DiskFault& f);

    const std::vector<DiskFault>& list() const { return list_; }
    bool                          empty() const { return list_.empty(); }

    // The whole table as one string, entries split by "; " -- the machine-file `faults` key.
    // fromText() is atomic: a bad entry changes nothing.
    std::string toText() const;
    bool        fromText(const std::string& text, std::string& err);

    // The table and its counters, for SNAPSHOT / RESTORE.
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    std::vector<DiskFault> list_;
    std::string            refused_[4];  // by FaultKind: empty = taken
};

} // namespace altair
