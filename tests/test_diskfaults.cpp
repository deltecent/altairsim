// DiskFaults -- the bad-sector overlay of one mounted diskette (#966).

#include "core/statefile.h"
#include "host/diskfaults.h"
#include "test.h"

#include <string>

using namespace altair;

void test_diskfaults() {
    SECTION("DiskFaults: a spec parses, and spec() reads back the same");
    {
        DiskFault   f;
        std::string err;
        CHECK(DiskFaults::parse("5/0/3 data count=2", f, err), "a count fault parses");
        CHECK(f.track == 5 && f.head == 0 && f.sector == 3, "...its address");
        CHECK(f.kind == FaultKind::Data && f.count == 2 && f.left == 2 && f.hits == 0, "...its kind and count");
        CHECK(!f.hard() && f.armed(), "...armed, not hard");
        CHECK(f.spec() == "5/0/3 data count=2", "spec() is the text it was parsed from");

        CHECK(DiskFaults::parse("  12/*/*   ID ", f, err), "wildcards, any case, extra spaces");
        CHECK(f.track == 12 && f.head == -1 && f.sector == -1 && f.kind == FaultKind::Id, "...read as any");
        CHECK(f.hard() && f.armed(), "no count is a hard fault");
        CHECK(f.spec() == "12/*/* id", "...and spec() writes no count");

        CHECK(DiskFaults::parse("0/1/26 missing", f, err) && f.kind == FaultKind::Missing, "missing");
        CHECK(DiskFaults::parse("0/1/26 write COUNT=1", f, err) && f.kind == FaultKind::Write && f.count == 1,
              "write, count in any case");
    }

    SECTION("DiskFaults: a bad spec is refused with a reason");
    {
        DiskFault   f;
        std::string err;
        const char* bad[] = {"",           "5/0/3",          "5/0 data",        "5/0/3/1 data",
                             "x/0/3 data", "5/0/3 bad",      "5/0/3 data cnt=2", "5/0/3 data count=0",
                             "5/0/3 data count=", "5/0/3 data count=-1", "5/0/3 data count=2 x",
                             "5/256/3 data", "5/0/256 data", "65536/0/0 data", "5/0/3 data count=1000001"};
        for (const char* s : bad) {
            err.clear();
            CHECK(!DiskFaults::parse(s, f, err) && !err.empty(), (std::string("refused: '") + s + "'").c_str());
        }
    }

    SECTION("DiskFaults: find() matches the address and the kind, and only while armed");
    {
        DiskFaults  t;
        std::string err;
        CHECK(t.add("5/0/3 data count=2", err), "added a count fault");
        CHECK(t.add("7/*/* id", err), "added a hard fault on all of track 7");

        CHECK(t.find(5, 0, 3, FaultKind::Data) != nullptr, "the sector, the kind");
        CHECK(t.find(5, 0, 3, FaultKind::Id) == nullptr, "...not another kind");
        CHECK(t.find(5, 1, 3, FaultKind::Data) == nullptr, "...not another head");
        CHECK(t.find(5, 0, 4, FaultKind::Data) == nullptr, "...not another sector");
        CHECK(t.find(7, 1, 19, FaultKind::Id) != nullptr, "a wildcard covers any head and sector");

        DiskFault* f = t.find(5, 0, 3, FaultKind::Data);
        DiskFaults::use(*f);
        CHECK(f->hits == 1 && f->left == 1 && f->armed(), "one use: one hit, one left");
        DiskFaults::use(*f);
        CHECK(f->hits == 2 && f->left == 0 && !f->armed(), "two uses: worn off");
        CHECK(t.find(5, 0, 3, FaultKind::Data) == nullptr, "a worn-off fault is not found");
        CHECK(t.list().size() == 2, "...but it stays in the list, with its hits");

        DiskFault* h = t.find(7, 0, 1, FaultKind::Id);
        for (int i = 0; i < 50; ++i) DiskFaults::use(*h);
        CHECK(h->hits == 50 && h->armed(), "a hard fault never wears off");
    }

    SECTION("DiskFaults: add replaces the same address and kind; clear removes");
    {
        DiskFaults  t;
        std::string err;
        CHECK(t.add("5/0/3 data count=2", err), "added");
        DiskFaults::use(*t.find(5, 0, 3, FaultKind::Data));
        CHECK(t.add("5/0/3 data count=4", err) && t.list().size() == 1, "the same address and kind replaces");
        CHECK(t.list()[0].left == 4 && t.list()[0].hits == 0, "...counters and all");
        CHECK(t.add("5/0/3 write", err) && t.list().size() == 2, "another kind is another fault");
        CHECK(t.add("5/*/* id", err) && t.list().size() == 3, "a wildcard is another address");

        CHECK(t.clear("5/0/3 write", err) && t.list().size() == 2, "clear one kind");
        CHECK(!t.clear("5/0/3 write", err) && !err.empty(), "clear what is not there: refused");
        CHECK(!t.clear("5/0/9", err), "clear an address with no fault: refused");
        CHECK(!t.clear("5/0", err), "clear a bad address: refused");
        CHECK(t.clear("5/*/*", err) && t.list().size() == 1, "a wildcard clears only the wildcard entry");
        CHECK(t.clear("5/0/3", err) && t.empty(), "an address with no kind clears every kind");

        CHECK(t.add("1/0/1 data", err), "added again");
        t.clearAll();
        CHECK(t.empty(), "clearAll");
    }

    SECTION("DiskFaults: toText / fromText, the machine-file key");
    {
        DiskFaults  t;
        std::string err;
        CHECK(t.fromText("5/0/3 data count=2;  12/*/* id ;", err), "a list parses, with spaces and a trailing ;");
        CHECK(t.toText() == "5/0/3 data count=2; 12/*/* id", "toText writes it back");

        DiskFaults u;
        CHECK(u.fromText(t.toText(), err) && u.toText() == t.toText(), "the text round-trips");

        CHECK(!t.fromText("1/0/1 data; nonsense", err) && !err.empty(), "a bad entry is refused");
        CHECK(t.toText() == "5/0/3 data count=2; 12/*/* id", "...and changes nothing");

        CHECK(t.fromText("", err) && t.empty(), "an empty text is no faults");
    }

    SECTION("DiskFaults: serialize keeps the counters");
    {
        DiskFaults  t;
        std::string err;
        CHECK(t.fromText("5/0/3 data count=3; 7/0/1 write", err), "two faults");
        DiskFaults::use(*t.find(5, 0, 3, FaultKind::Data));
        DiskFaults::use(*t.find(7, 0, 1, FaultKind::Write));
        DiskFaults::use(*t.find(7, 0, 1, FaultKind::Write));

        StateWriter w;
        t.serialize(w);
        StateReader r(w.data());
        DiskFaults  u;
        u.deserialize(r);
        CHECK(r.ok() && r.remaining() == 0, "read back the whole record");
        CHECK(u.list().size() == 2, "both faults");
        CHECK(u.list()[0].left == 2 && u.list()[0].hits == 1 && u.list()[0].count == 3, "the count fault's counters");
        CHECK(u.list()[1].hard() && u.list()[1].hits == 2, "the hard fault's hits");
    }

    SECTION("DiskFaults: refuse() -- a kind the drive's medium cannot have (#970)");
    {
        DiskFaults  t;
        std::string err;
        t.refuse(FaultKind::Id, "no ID field here");
        CHECK(!t.add("1/0/2 id", err) && err == "bad fault '1/0/2 id': no ID field here", err.c_str());
        CHECK(t.add("1/0/2 data", err) && t.list().size() == 1, "another kind is taken");
        CHECK(!t.fromText("3/0/1 missing; 4/0/1 ID", err) && err.find("no ID field here") != std::string::npos,
              "fromText refuses it");
        CHECK(t.toText() == "1/0/2 data", "...and changes nothing");
        t.clearAll();
        CHECK(!t.add("1/0/2 id", err), "clearAll keeps the refusal");
        StateWriter w;
        t.serialize(w);
        StateReader r(w.data());
        t.deserialize(r);
        CHECK(!t.add("1/0/2 id", err), "...and so does deserialize");
    }
}
