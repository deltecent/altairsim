// The CompuPro M-Drive/H on a real bus: the shift-register address port, the counter, the
// cards that are and are not fitted, CompuPro's own driver and BIOS sizing loop as the
// oracle, the base, both resets and POWER, and the snapshot.
// See reference/CompuPro M-Drive Manual.md and src/boards/compupro-mdriveh.h.

#include "test.h"

#include "boards/compupro-mdriveh.h"
#include "core/machine.h"
#include "core/statefile.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

struct Rig {
    Machine       m;
    MDriveHBoard* md = nullptr;
    uint8_t       base = 0xC6;

    explicit Rig(int boards = 1, const char* fill = "zero") {
        std::string err;
        m.bus.setVerify(true);
        md = dynamic_cast<MDriveHBoard*>(m.add("mdriveh", "md0", err));
        setProperty(*md, "fill", fill, err);
        setProperty(*md, "boards", std::to_string(boards), err);
        m.power();
    }

    // Three OUTs to the address port, high byte first: what SETMDRV does.
    void addr(uint32_t a) {
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)(a >> 16));
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)(a >> 8));
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)a);
    }
    uint8_t in() { return m.bus.ioRead(base); }
    void    out(uint8_t v) { m.bus.ioWrite(base, v); }

    // ---- The manual's sample driver (pp.13-16), and READHMD/WRITHMD on TMXBIOS ----
    // The record for track t, sector s (0-based) is at s<<16 | t<<7; its parity byte is at
    // s<<16 | t and holds 80H plus the sum of the 128 bytes.
    void setmdrv(int s, uint16_t hl) {
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)s);
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)(hl >> 8));
        m.bus.ioWrite((uint8_t)(base + 1), (uint8_t)hl);
    }
    void writeRecord(int t, int s, const uint8_t* buf) {
        uint8_t b = 0x80;
        setmdrv(s, (uint16_t)(t * 128));
        for (int i = 0; i < 128; ++i) { out(buf[i]); b = (uint8_t)(b + buf[i]); }
        setmdrv(s, (uint16_t)t);
        out(b);
    }
    // True when the parity byte matches (the driver's SUB B gives 0).
    bool readRecord(int t, int s, uint8_t* buf) {
        uint8_t b = 0x80;
        setmdrv(s, (uint16_t)(t * 128));
        for (int i = 0; i < 128; ++i) { buf[i] = in(); b = (uint8_t)(b + buf[i]); }
        setmdrv(s, (uint16_t)t);
        return (uint8_t)(in() - b) == 0;
    }

    // The cold-boot sizing in CompuPro's BIOS: at track 4, sector 8k (the first record of
    // card k), read, write back and read again; a parity error ends the count.
    int sizeCards() {
        uint8_t buf[128];
        int k = 0;
        for (; k < 8; ++k) {
            readRecord(4, 8 * k, buf);
            writeRecord(4, 8 * k, buf);
            if (!readRecord(4, 8 * k, buf)) break;
        }
        return k;
    }
};

}  // namespace

void test_mdriveh() {
    SECTION("M-Drive/H -- the address port is a shift register, loaded high byte first");
    {
        Rig k;
        k.addr(0x012345);
        CHECK(k.md->address() == 0x012345, "three OUTs load the 22-bit address, high first");

        k.m.bus.ioWrite(0xC7, 0xFF);
        k.m.bus.ioWrite(0xC7, 0x00);
        k.m.bus.ioWrite(0xC7, 0x01);
        CHECK(k.md->address() == 0x3F0001, "only bits 0-5 of the first byte are kept");

        k.addr(0x0A0B0C);
        k.m.bus.ioWrite(0xC7, 0x77);
        CHECK(k.md->address() == 0x0B0C77, "a fourth OUT shifts the address up one more byte");

        CHECK(k.m.bus.ioRead(0xC7) == 0xFF, "an IN from the address port is not decoded");
    }

    SECTION("M-Drive/H -- the data port counts up after each access, across cards, and wraps");
    {
        Rig k(2);
        k.addr(0x000100);
        for (int i = 0; i < 4; ++i) k.out((uint8_t)(0x10 + i));
        CHECK(k.md->address() == 0x000104, "each OUT counts the address up");
        k.addr(0x000100);
        bool ok = true;
        for (int i = 0; i < 4; ++i) ok = ok && k.in() == 0x10 + i;
        CHECK(ok, "each IN reads the next byte");
        CHECK(k.md->address() == 0x000104, "each IN counts the address up");

        k.addr(0x07FFFE);
        k.out(0xA1);
        k.out(0xA2);
        k.out(0xB1);
        CHECK(k.md->peekRam(0x07FFFF) == 0xA2 && k.md->peekRam(0x080000) == 0xB1,
              "a run crosses from card 0 to card 1 at 080000");

        k.addr(0x3FFFFF);
        k.in();
        CHECK(k.md->address() == 0x000000, "the counter wraps from 3FFFFF to 000000");
    }

    SECTION("M-Drive/H -- an address on a card that is not fitted reads FF, and still counts");
    {
        Rig k(2);
        k.addr(0x100000);   // card 2: MA21-MA19 = 010
        k.out(0x55);
        CHECK(k.md->address() == 0x100001, "a write to a missing card still counts");
        k.addr(0x100000);
        CHECK(k.in() == 0xFF, "a missing card reads FF: nothing drives the bus");
        CHECK(k.md->address() == 0x100001, "a read of a missing card still counts");

        k.addr(0x0FFFFF);
        k.out(0x66);
        CHECK(k.md->peekRam(0x0FFFFF) == 0x66, "the last byte of card 1 is fitted");
    }

    SECTION("M-Drive/H -- CompuPro's driver writes and reads records with their parity");
    {
        Rig k(1, "random");
        uint8_t buf[128], got[128];
        for (int i = 0; i < 128; ++i) buf[i] = (uint8_t)(i * 7 + 3);
        k.writeRecord(511, 7, buf);
        k.writeRecord(4, 0, buf);

        uint8_t sum = 0x80;
        for (int i = 0; i < 128; ++i) sum = (uint8_t)(sum + buf[i]);
        CHECK(k.md->peekRam(0x070000 | (511u << 7)) == buf[0], "the record is at s<<16 | t<<7");
        CHECK(k.md->peekRam(0x070000 | 511u) == sum, "its parity byte, 80H + the sum, is at s<<16 | t");

        bool same = k.readRecord(511, 7, got);
        for (int i = 0; i < 128; ++i) same = same && got[i] == buf[i];
        CHECK(same, "the driver reads the record back with good parity");

        k.setmdrv(7, (uint16_t)(511 * 128 + 5));
        k.out((uint8_t)(buf[5] ^ 1));
        CHECK(!k.readRecord(511, 7, got), "a changed byte fails the parity check");
    }

    SECTION("M-Drive/H -- the BIOS sizing loop counts exactly the cards fitted");
    for (int n : {1, 3, 8}) {
        Rig k(n, "random");
        CHECK(k.sizeCards() == n, ("boards = " + std::to_string(n) + " sizes as " +
                                   std::to_string(n) + " cards").c_str());
    }

    SECTION("M-Drive/H -- the base is even, and moving it moves both ports");
    {
        Rig k;
        std::string err;
        CHECK(!setProperty(*k.md, "port", "C7", err), "an odd base is refused");
        CHECK(setProperty(*k.md, "port", "40", err), "the base moves to 40");
        k.base = 0x40;
        k.addr(0x000010);
        k.out(0x99);
        k.addr(0x000010);
        CHECK(k.in() == 0x99, "the data and address ports moved to 40 and 41");
        CHECK(k.m.bus.ioRead(0xC6) == 0xFF, "C6 is free again");
    }

    SECTION("M-Drive/H -- both resets keep the RAM and the counter; POWER refills the RAM");
    {
        Rig k(1, "random");
        k.addr(0x001234);
        k.out(0x5A);
        k.m.reset(Reset::Bus);
        k.m.reset(Reset::PowerOn);
        CHECK(k.md->address() == 0x001235, "neither reset touches the counter");
        CHECK(k.md->peekRam(0x001234) == 0x5A, "neither reset touches the RAM");

        k.m.power();
        const uint8_t a = k.md->peekRam(0x001234), b = k.md->peekRam(0x001235);
        CHECK(k.md->address() == 0, "POWER sets the counter to 0");
        k.out(0x5A);
        k.m.power();
        CHECK(k.md->peekRam(0x001234) == a && k.md->peekRam(0x001235) == b &&
                  k.md->peekRam(0) != 0x5A,
              "one seed gives the same contents at each POWER");

        std::string err;
        setProperty(*k.md, "seed", "99", err);
        k.m.power();
        bool differ = false;
        for (uint32_t i = 0; i < 64; ++i) differ = differ || k.md->peekRam(0x001234 + i) != 0;
        CHECK(differ, "fill=random is not zeros");

        setProperty(*k.md, "fill", "zero", err);
        k.m.power();
        bool zero = true;
        for (uint32_t i = 0; i < MDriveHBoard::kCardBytes; i += 4093) zero = zero && k.md->peekRam(i) == 0;
        CHECK(zero, "fill=zero gives zeros at POWER");
    }

    SECTION("M-Drive/H -- boards: 1 to 8, and new cards leave the old ones alone");
    {
        Rig k;
        std::string err;
        CHECK(!setProperty(*k.md, "boards", "0", err), "boards = 0 is refused");
        CHECK(!setProperty(*k.md, "boards", "9", err), "boards = 9 is refused");
        k.addr(0x000020);
        k.out(0xC3);
        CHECK(setProperty(*k.md, "boards", "4", err), "boards = 4 is accepted");
        CHECK(k.md->peekRam(0x000020) == 0xC3, "card 0 keeps its bytes when cards are added");
        k.addr(0x180000);
        CHECK(k.in() == 0x00, "card 3 is fitted, and filled per fill = zero");
        CHECK(setProperty(*k.md, "boards", "1", err), "boards = 1 again");
        k.addr(0x080000);
        CHECK(k.in() == 0xFF, "card 1 is gone");
    }

    SECTION("M-Drive/H -- the snapshot carries the counter, the card count and the RAM");
    {
        Rig k(2, "random");
        k.addr(0x0C0010);
        k.out(0x42);
        StateWriter w;
        k.md->serialize(w);

        Rig h(1, "zero");
        StateReader r(w.data());
        h.md->deserialize(r);
        CHECK(r.ok(), "restore consumed the blob cleanly");
        CHECK(h.md->address() == 0x0C0011, "the counter travelled");
        CHECK(h.md->peekRam(0x0C0010) == 0x42, "the RAM on card 1 travelled");
        CHECK(h.md->properties()[1].get().i() == 2, "the card count travelled");
        bool same = true;
        for (uint32_t i = 0; i < 2 * MDriveHBoard::kCardBytes; i += 997)
            same = same && h.md->peekRam(i) == k.md->peekRam(i);
        CHECK(same, "every byte travelled");
    }
}
