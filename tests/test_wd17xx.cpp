#include "test.h"

#include "chips/wd17xx.h"
#include "core/clock.h"

#include <functional>
#include <map>
#include <vector>

using namespace altair;

namespace {

// ---------------------------------------------------------------------------
// A DRIVE MADE OF NOTHING. The ScriptedStream of the disk world (see media.h) --
// no filesystem, no DiskImage, no geometry probe. The chip cannot tell the
// difference, which is the entire point of FloppyDrive being an interface: these
// tests exercise the FD1771 and NOT the 300 lines of board underneath a real one.
// ---------------------------------------------------------------------------
struct FakeDrive : FloppyDrive {
    struct Sec {
        FloppyDrive::SectorId id{};
        std::vector<uint8_t>  data;
    };

    int  head    = 0;      // where the HEAD is. Not the track register -- see wd17xx.h.
    int  tracks  = 77;
    bool isReady = true;
    bool isWp    = false;
    bool atIndex = false;
    int  steps   = 0;      // how many STEP pulses we were given, for the timing tests

    std::map<int, std::vector<Sec>> track;  // by PHYSICAL track, in rotational order

    // Lay down `n` sectors of `size` bytes on physical track `t`, with the ID field
    // claiming to be track `idTrack` -- which is normally the same, and is NOT when a
    // test wants to prove the chip notices.
    void format(int t, int n, int size, int idTrack = -1) {
        if (idTrack < 0) idTrack = t;
        auto& v = track[t];
        v.clear();
        for (int s = 1; s <= n; ++s) {
            Sec sec;
            sec.id.track      = idTrack;
            sec.id.sector     = s;
            sec.id.size       = size;
            sec.id.lengthCode = 0;  // IBM 3740: 128 << 0
            sec.data.assign((size_t)size, (uint8_t)(0xA0 + s));
            v.push_back(sec);
        }
    }

    Sec* find(int t, int s) {
        auto it = track.find(t);
        if (it == track.end()) return nullptr;
        for (auto& sec : it->second)
            if (sec.id.sector == s) return &sec;
        return nullptr;
    }

    bool ready() const override { return isReady; }
    bool writeProtected() const override { return isWp; }
    bool trackZero() const override { return head == 0; }
    bool index() const override { return atIndex; }
    int  headTrack() const override { return head; }

    void step(bool inward) override {
        ++steps;
        if (inward) {
            if (head < tracks - 1) ++head;
        } else if (head > 0) {
            --head;  // ...and a step out at track 0 goes nowhere. The head is on its stop.
        }
    }

    int sectorCount() const override {
        auto it = track.find(head);
        return it == track.end() ? 0 : (int)it->second.size();
    }

    bool sectorIdAt(int i, SectorId& out) const override {
        auto it = track.find(head);
        if (it == track.end() || i < 0 || i >= (int)it->second.size()) return false;
        out = it->second[(size_t)i].id;
        return true;
    }

    bool readData(const SectorId& id, uint8_t* buf, size_t* n) override {
        Sec* s = find(head, id.sector);
        if (!s || *n < s->data.size()) return false;
        for (size_t i = 0; i < s->data.size(); ++i) buf[i] = s->data[i];
        *n = s->data.size();
        return true;
    }

    bool writeData(const SectorId& id, const uint8_t* buf, size_t n) override {
        Sec* s = find(head, id.sector);
        if (!s) return false;
        s->data.assign(buf, buf + n);
        // THE MARK THE CHIP HANDED US, not the one that was there. `id.deleted` is the
        // command's a1a0 field, and a drive that ignored it would make the chip's a1a0
        // decode unobservable -- which is how it stayed broken.
        s->id.deleted = id.deleted;
        return true;
    }
};

// Spin the clock until the chip goes idle, calling `svc` after every poll -- which is
// what a card's deadline handler does, and is where a driver services DRQ.
//
// The trailing svc() is not a nicety: the LAST byte of a read lands in the data register
// in the very poll that also drops BUSY (the chip is done; the byte is still there to be
// collected). A loop that only services while busy would drop it every time and every
// read would come up one byte short.
template <class F>
uint64_t spin(Wd1771& f, Clock& clk, F svc, uint64_t limit = 60000000) {
    const uint64_t t0 = clk.now();
    while (f.busy() && clk.now() - t0 < limit) {
        clk.advance(8);
        f.poll(clk);
        svc(f);
    }
    svc(f);
    return clk.now() - t0;
}

inline void idle(Wd1771&) {}

} // namespace

void test_wd17xx() {
    SECTION("chips/wd17xx: the FD1771");

    // ---- TABLE 1, AND THE REASON THIS CHIP HAS ITS OWN FILE ----
    //
    // 6/6/10/20 ms at CLK=2 MHz. If a future edit "fixes" these to 6/12/20/30 it has
    // been done against the WD1770 data sheet, which is a DIFFERENT CHIP that happens
    // to be in reference/. This test is the tripwire.
    {
        const int wantMs[4] = {6, 6, 10, 20};
        for (int r = 0; r < 4; ++r) {
            Clock clk;
            FakeDrive d;
            Wd1771    f("fdc");
            f.attach(&d);
            f.powerOn(clk);
            d.head = 5;

            // Step In, u=1, V=0, rate r. Exactly one step, so the elapsed time IS the rate.
            f.writeCommand((uint8_t)(0x50 | r), clk);
            const uint64_t took = spin(f, clk, idle);

            const uint64_t want = (uint64_t)(clk.hz() * wantMs[r] / 1000);
            char msg[96];
            std::snprintf(msg, sizeof msg, "step rate r=%d is %d ms", r, wantMs[r]);
            // One 8-T-state tick of slop: spin() cannot land on the exact T-state.
            CHECK(took >= want && took < want + 16, msg);
        }
    }

    // ---- RESTORE homes the head, and the track register follows it ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.head  = 12;
        d.format(0, 26, 128);

        f.writeCommand(0x03, clk);  // Restore, 20 ms step
        spin(f, clk, idle);

        CHECK(d.head == 0, "restore drives the head to track 0");
        CHECK(f.readTrackReg() == 0, "restore zeroes the track register");
        CHECK(f.intrq(), "restore raises INTRQ on completion");
        CHECK((f.readStatus(clk) & 0x04) != 0, "type I status S2 is TRACK 0, and it is set");
        CHECK(!f.intrq(), "...and reading status clears INTRQ");
    }

    // ---- SEEK: the DATA register is the target, the TRACK register is where we are ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(30, 26, 128);

        f.writeTrackReg(0);
        f.writeData(30, clk);       // the destination
        f.writeCommand(0x10, clk);  // Seek, no verify
        spin(f, clk, idle);

        CHECK(d.head == 30, "seek moves the head to the data register's track");
        CHECK(f.readTrackReg() == 30, "seek updates the track register");
        CHECK(d.steps == 30, "...one STEP pulse per track, and not one more");
    }

    // ---- SEEK ERROR: the one thing that catches a drive that missed a step ----
    //
    // The verify compares the TRACK REGISTER against the track number RECORDED IN THE ID
    // FIELD. Here the head lands on physical track 10, but the disk in the drive was
    // formatted with track 10's ID fields claiming to be track 40. A model that compared
    // the track register to the HEAD POSITION would call this a clean seek.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(10, 26, 128, /*idTrack=*/40);

        f.writeTrackReg(0);
        f.writeData(10, clk);
        f.writeCommand(0x14, clk);  // Seek WITH verify (V=1)
        spin(f, clk, idle);

        CHECK(d.head == 10, "the head did go where it was told");
        CHECK((f.readStatus(clk) & 0x10) != 0, "but the ID field disagrees: S4 SEEK ERROR");
    }
    {
        // ...and the same seek onto a correctly-formatted track verifies clean.
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(10, 26, 128);

        f.writeData(10, clk);
        f.writeCommand(0x14, clk);
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x10) == 0, "a verify against a matching ID field passes");
    }

    // ---- READ SECTOR ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(3, 26, 128);
        d.head = 3;

        f.writeTrackReg(3);
        f.writeSectorReg(7);
        std::vector<uint8_t> got;
        f.writeCommand(0x8C, clk);  // Read Sector, m=0, b=1, E=1
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });

        CHECK(got.size() == 128, "read sector delivers exactly one sector");
        CHECK(!got.empty() && got[0] == 0xA7, "...and it is the sector we asked for");
        const uint8_t st = f.readStatus(clk);
        CHECK((st & 0x04) == 0, "a serviced read loses no data");
        CHECK((st & 0x10) == 0, "...and finds the record");
    }

    // ---- LOST DATA: a guest that does not service DRQ in one byte time loses the byte ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);
        spin(f, clk, idle);  // service NOTHING

        CHECK((f.readStatus(clk) & 0x04) != 0, "an unserviced read sets S2 LOST DATA");
    }

    // ---- RECORD NOT FOUND ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(99);  // there is no sector 99
        f.writeCommand(0x8C, clk);
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x10) != 0, "a missing sector sets S4 RECORD NOT FOUND");
    }

    // ---- THE STATUS REGISTER IS SIX REGISTERS (Table 6) ----
    //
    // The single easiest thing to get wrong in this chip. Bit 2 is TRACK 0 after a Type I
    // and LOST DATA after a read; bit 4 is SEEK ERROR after a Type I and RECORD NOT FOUND
    // after a read. Same disk, same head, same instant -- two different answers, decided
    // by which command LAST RAN.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);  // head is at track 0

        f.writeCommand(0x03, clk);  // Restore -> Type I context
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x04) != 0, "after a Type I, bit 2 means TRACK 0 (set)");

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);  // Read -> Read context, and we service it properly
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);
        });
        // The head has not moved. It is STILL on track 0. But bit 2 no longer says so.
        CHECK(d.head == 0, "the head is still on track 0");
        CHECK((f.readStatus(clk) & 0x04) == 0,
              "after a Read, bit 2 means LOST DATA -- and nothing was lost");
    }

    // ---- THE RECORD TYPE IS TWO BITS, because the 1771 has four data address marks ----
    // This is the deepest difference from a 179x, where it is one bit. F8 (deleted) is 11.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        d.find(0, 1)->id.deleted = true;

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);
        });
        const uint8_t st = f.readStatus(clk);
        CHECK((st & 0x60) == 0x60, "a deleted data mark sets BOTH record-type bits (S6|S5)");
    }

    // ---- WRITE SECTOR ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(2);
        std::vector<uint8_t> out((size_t)128, 0x5A);
        size_t               i = 0;
        f.writeCommand(0xAC, clk);  // Write Sector, m=0, b=1, E=1, a1a0=00 (FB)
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq() && i < out.size()) c.writeData(out[i++], clk);
        });

        CHECK(i == 128, "write sector consumes exactly one sector's worth of bytes");
        CHECK(d.find(0, 2)->data[0] == 0x5A, "...and the bytes reach the drive");
        CHECK(d.find(0, 2)->data[127] == 0x5A, "...all of them");
        CHECK((f.readStatus(clk) & 0x04) == 0, "a serviced write loses no data");
    }

    // ---- THE FIRST DRQ OF A WRITE IS FATAL, AND THE REST ARE MERELY LOSSY ----
    //
    // "the Write Gate output is made active IF the DRQ is serviced. If DRQ has not been
    // serviced, the command is terminated and the Lost Data status bit is set."  Nothing
    // is written at all. This is a real piece of hardware design -- the chip will not open
    // the write gate on a driver that is not keeping up -- and a model that wrote a sector
    // of zeros here would silently destroy the disk the real card was protecting.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        const uint8_t before = d.find(0, 3)->data[0];

        f.writeSectorReg(3);
        f.writeCommand(0xAC, clk);
        spin(f, clk, idle);  // never service the first DRQ

        CHECK((f.readStatus(clk) & 0x04) != 0, "a missed FIRST DRQ sets LOST DATA");
        CHECK(d.find(0, 3)->data[0] == before, "...and NOTHING is written to the disk");
    }
    {
        // ...but a byte missed part-way through writes a ZERO and carries on. The sector
        // lands on the disk with a hole in it, exactly as the hardware leaves it.
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(4);

        // MISSING A DRQ MEANS LETTING IT LAPSE -- not "declining once and servicing it on
        // the next tick". DRQ stays up for a whole byte time (64 T-states), and spin()
        // ticks every 8, so a service routine that merely skips one tick still makes the
        // deadline with room to spare. To really lose the byte we have to sit on our hands
        // until the CHIP gives up on it (which is when it clears DRQ and writes its zero).
        int  n        = 0;
        bool lapsing  = false;
        bool didLapse = false;
        f.writeCommand(0xAC, clk);
        spin(f, clk, [&](Wd1771& c) {
            if (lapsing) {
                if (!c.drq()) lapsing = false;  // the chip timed it out; carry on
                return;
            }
            if (!c.drq()) return;
            if (n == 64 && !didLapse) {
                lapsing = didLapse = true;  // let byte 64's request die of old age
                return;
            }
            c.writeData((uint8_t)0xC3, clk);
            ++n;
        });

        const auto& got = d.find(0, 4)->data;
        CHECK((f.readStatus(clk) & 0x04) != 0, "a missed MIDDLE DRQ sets LOST DATA");
        CHECK(got.size() == 128, "...but the sector is still written, in full");
        CHECK(got[0] == 0xC3, "...with the bytes that arrived");
        CHECK(got[64] == 0x00, "...and a byte of ZEROS where the guest was late");
    }

    // ---- WRITE PROTECT terminates a write immediately, before the head moves ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        d.isWp = true;
        const uint8_t before = d.find(0, 1)->data[0];

        f.writeSectorReg(1);
        f.writeCommand(0xAC, clk);
        spin(f, clk, idle);

        CHECK((f.readStatus(clk) & 0x40) != 0, "a write to a protected disk sets S6 PROTECTED");
        CHECK(d.find(0, 1)->data[0] == before, "...and writes nothing");
        CHECK(!f.busy(), "...and does not leave the chip busy");
    }

    // ---- NOT READY: Type II will not execute. Type I RUNS ANYWAY. ----
    //
    // "The Seek or Step commands are performed regardless of the state of the READY
    // input." That is not a quirk -- it is how a Restore can home a head before anyone
    // has put a disk in the drive.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.isReady = false;
        d.head    = 9;

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);  // Read
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x80) != 0, "a read on a not-ready drive reports S7 NOT READY");

        f.writeCommand(0x03, clk);  // Restore
        spin(f, clk, idle);
        CHECK(d.head == 0, "...but a Restore homes the head regardless of READY");
    }

    // ---- FORCE INTERRUPT ----
    {
        // While BUSY: drop BUSY, and LEAVE THE OTHER STATUS BITS ALONE -- so the context
        // stays whatever it was, and a driver that aborts a read still sees a read's status.
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        d.head = 0;

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);  // start a read (E=1: a 10 ms head settle first)
        clk.advance(200);
        f.poll(clk);
        CHECK(f.busy(), "the read is running");

        f.writeCommand(0xD0, clk);  // Force Interrupt, all I bits clear
        CHECK(!f.busy(), "force interrupt drops BUSY");
        CHECK(!f.intrq(), "...and D0 (no I bits) raises NO interrupt");

        // Bit 2 must STILL mean LOST DATA, not TRACK 0 -- the context was not reset. The
        // head is on track 0, so a chip that had reverted to Type I would light bit 2 here.
        CHECK((f.readStatus(clk) & 0x04) == 0,
              "the status context survives an abort: bit 2 is still LOST DATA");
    }
    {
        // While IDLE: "Status reflects the Type I commands" -- the context RESETS.
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);  // a read, run to completion, unserviced
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x04) != 0, "the read context has LOST DATA set");

        f.writeCommand(0xD0, clk);  // now force-interrupt an IDLE chip
        // The head is at track 0, and we are back in the Type I context, so bit 2 is TRACK 0.
        CHECK((f.readStatus(clk) & 0x04) != 0, "an idle force interrupt reverts to Type I: bit 2 is TRACK 0");
        CHECK((f.readStatus(clk) & 0x10) == 0, "...with the error bits cleared");

        f.writeCommand(0xD8, clk);  // I3 -- immediate interrupt
        CHECK(f.intrq(), "D8 (I3) raises INTRQ immediately");
    }

    // ---- MASTER RESET -- pin 19, and it does NOT merely idle the chip ----
    //
    // "loads '03' into the command register ... When MR is brought to a logic high, a
    // RESTORE COMMAND IS EXECUTED, regardless of the state of the Ready signal."  A
    // front-panel RESET drives the head home with no software involved.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.head    = 40;
        d.isReady = false;  // ...and it happens even with no disk in the drive

        f.masterReset(clk);
        spin(f, clk, idle);

        CHECK(d.head == 0, "a master reset restores the head to track 0, all by itself");
        CHECK(f.readTrackReg() == 0, "...and zeroes the track register");
    }

    // ---- READ ADDRESS: six bytes, and it rewrites the sector register ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(5, 26, 128);
        d.head = 5;
        f.writeTrackReg(5);

        std::vector<uint8_t> got;
        f.writeCommand(0xC4, clk);  // Read Address, E=1
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });

        CHECK(got.size() == 6, "read address delivers six bytes");
        CHECK(!got.empty() && got[0] == 5, "byte 1 is the TRACK");
        CHECK(got.size() > 1 && got[1] == 0, "byte 2 is ZERO -- the 1771 has no side-select pin");
        CHECK(got.size() > 2 && got[2] == 1, "byte 3 is the SECTOR");
        CHECK(got.size() > 3 && got[3] == 0, "byte 4 is the sector LENGTH code");
        CHECK(f.readSectorReg() == 1, "...and the ID's sector is written into the sector register");
    }

    // ---- A COMMAND MUST NOT INHERIT THE LAST COMMAND'S SECTOR ----
    //
    // The data-field CRC belongs to the RECORD the last Type II command found. Read
    // Address never touches a data field at all -- it reads an ID field -- so if it can
    // still see the previous read's rotten CRC, it will report a CRC error on a sector it
    // did not read, and a formatter probing an unknown disk will condemn a good track.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        d.find(0, 1)->id.dataCrcOk = false;  // one rotten sector...

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);  // ...read it, and get the CRC error we deserve
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);
        });
        CHECK((f.readStatus(clk) & 0x08) != 0, "a bad data-field CRC sets S3 on the read");

        f.writeCommand(0xC4, clk);  // now a Read Address, which reads NO data field
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);
        });
        CHECK((f.readStatus(clk) & 0x08) == 0,
              "...and the NEXT command does not inherit it");
    }

    // ---- MULTIPLE RECORDS run until the sector register leaves the track ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 4, 128);  // only four sectors on this track

        f.writeSectorReg(3);  // start at 3: we should get 3 and 4, then stop
        std::vector<uint8_t> got;
        f.writeCommand(0x9C, clk);  // Read Sector, m=1, b=1, E=1
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });

        CHECK(got.size() == 256, "a multi-record read runs to the end of the track and stops");
        CHECK(!got.empty() && got[0] == 0xA3, "...starting at the sector register");
        CHECK(got.size() == 256 && got[128] == 0xA4, "...and running on into the next one");

        // ...AND IT STOPS BY FAILING. The sector register walks off the end of the track,
        // goes back into the ID search, and the search finds nothing -- so the NORMAL
        // termination of a multi-record transfer is RECORD NOT FOUND. Drivers written
        // against the real part know this and read it as success. A model that finished
        // clean here hands them a status they never see on hardware.
        CHECK((f.readStatus(clk) & 0x10) != 0,
              "a multi-record read terminates with S4 RECORD NOT FOUND -- that IS its exit");
    }

    // ---- a1a0: THE WRITE COMMAND CHOOSES THE DATA ADDRESS MARK ----
    //
    // a1a0=11 writes an F8 (Deleted Data Mark). The mark written is the one the COMMAND
    // asked for, never the one that happened to be on the disk -- so a normal write over a
    // deleted sector must also CLEAR the mark.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        std::vector<uint8_t> out((size_t)128, 0x11);
        size_t               i = 0;
        f.writeSectorReg(5);
        f.writeCommand(0xAF, clk);  // Write, m=0, b=1, E=1, a1a0=11 -> F8, deleted
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq() && i < out.size()) c.writeData(out[i++], clk);
        });
        CHECK(d.find(0, 5)->id.deleted, "a1a0=11 writes a DELETED data mark");

        // ...and now a plain write over the top of it must put the mark back to FB.
        i = 0;
        f.writeSectorReg(5);
        f.writeCommand(0xAC, clk);  // a1a0=00 -> FB, a normal record
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq() && i < out.size()) c.writeData(out[i++], clk);
        });
        CHECK(!d.find(0, 5)->id.deleted, "...and a1a0=00 clears it again");
    }

    // ---- A DELETED *AND* CORRUPT SECTOR IS STILL DELETED ----
    //
    // The record type is latched off the address mark at the START of the data field --
    // before a byte moves, and long before the CRC at the far end is checked. Latch it
    // after the CRC check instead and this sector reports "normal record, CRC error",
    // which tells a BIOS "broken" when the truth is "deleted AND broken".
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);
        d.find(0, 1)->id.deleted   = true;
        d.find(0, 1)->id.dataCrcOk = false;

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);
        });
        const uint8_t st = f.readStatus(clk);
        CHECK((st & 0x08) != 0, "a corrupt sector reports S3 CRC ERROR...");
        CHECK((st & 0x60) == 0x60, "...and a deleted one is STILL reported deleted (S6|S5)");
    }

    // ---- S5 IS HLD *AND* HLT: the head is not engaged the instant we ask for it ----
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeData(0, clk);
        f.writeCommand(0x18, clk);  // Seek, h=1 (load the head), V=0
        f.poll(clk);
        CHECK((f.readStatus(clk) & 0x20) == 0,
              "S5 is 0 immediately after the head is commanded down -- HLT has not risen");

        clk.advance((uint64_t)(clk.hz() / 100));  // 10 ms
        f.poll(clk);
        CHECK((f.readStatus(clk) & 0x20) != 0, "...and 1 once the 10 ms settle has passed");

        // h=0 DE-ASSERTS HLD. Without the else, S5 latches high for the rest of time.
        f.writeCommand(0x10, clk);  // Seek, h=0
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x20) == 0, "a command with h=0 lets the head back up");
    }

    // ---- THE VERIFY JUDGES THE *FIRST* GOOD ID FIELD, and does not go shopping ----
    //
    // Track 20 is formatted so its FIRST ID field claims track 99 and the rest claim 20. A
    // chip that scanned for any match would verify happily. The real one reads the first
    // legible ID field, finds 99 != 20, and faults.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(20, 26, 128);
        d.track[20][0].id.track = 99;  // ...only the first one is a liar

        f.writeData(20, clk);
        f.writeCommand(0x14, clk);  // Seek with verify
        spin(f, clk, idle);
        CHECK((f.readStatus(clk) & 0x10) != 0,
              "the FIRST good ID field decides the verify -- a later match does not rescue it");
    }

    // ---- FORCE INTERRUPT PRESERVES DRQ, because S1 IS DRQ ----
    // "the rest of the status bits are unchanged" has no exception for the untidy one.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(1);
        f.writeCommand(0x8C, clk);
        // Run far enough in that the chip has a byte waiting and DRQ is up.
        while (!f.drq() && f.busy()) {
            clk.advance(8);
            f.poll(clk);
        }
        CHECK(f.drq(), "the read has a byte waiting");

        f.writeCommand(0xD0, clk);  // abort
        CHECK(!f.busy(), "force interrupt drops BUSY");
        CHECK(f.drq(), "...but leaves DRQ alone -- it is one of the status bits it must not touch");
    }

    // ---- READING THE DATA REGISTER DOES NOT SERVICE A *WRITE* ----
    //
    // Pin 38: DRQ is reset "through reading or loading the DR in Read or Write operation,
    // RESPECTIVELY." A read of the DR mid-write answers nothing -- and if it cleared DRQ,
    // the streamer would see the request satisfied and Lost Data would be unreachable.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeSectorReg(1);
        f.writeCommand(0xAC, clk);
        spin(f, clk, [&](Wd1771& c) {
            if (c.drq()) (void)c.readData(clk);  // read, never load. This services NOTHING.
        });
        CHECK((f.readStatus(clk) & 0x04) != 0,
              "reading the DR during a write does not service the DRQ: LOST DATA");
    }

    // ---- MASTER RESET LEAVES THE SECTOR AND DATA REGISTERS ALONE ----
    // The data sheet's whole account of MR is: load 0x03 into the COMMAND register, force
    // Not Ready while held, and Restore on release. SR and DR are not in that list.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.head = 6;

        f.writeSectorReg(0x1D);
        f.masterReset(clk);
        spin(f, clk, idle);

        CHECK(d.head == 0, "MR restores the head");
        CHECK(f.readTrackReg() == 0, "...and the track register follows it home");
        CHECK(f.readSectorReg() == 0x1D, "...but the SECTOR register survives a reset");
    }

    // ---- WRITE TRACK on a drive that has no bit-level image: WRITE FAULT, and it SAYS SO ----
    //
    // A raw .DSK has no gaps and no address marks in it, so there is nowhere for a format
    // to go. The chip does not invent 5,208 bytes of track: it sets the bit the hardware
    // would set (S5, WRITE FAULT) and puts a sentence where the operator can read it.
    {
        Clock clk;
        FakeDrive d;
        Wd1771    f("fdc");
        f.attach(&d);
        f.powerOn(clk);
        d.format(0, 26, 128);

        f.writeCommand(0xF4, clk);  // Write Track
        spin(f, clk, idle);

        CHECK((f.readStatus(clk) & 0x20) != 0, "write track on a sector-image drive sets S5 WRITE FAULT");
        CHECK(!f.drainLog().empty(), "...and says why, out loud");
    }
}
