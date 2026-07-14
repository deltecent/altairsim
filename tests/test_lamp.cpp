// THE DEVELOPER GUIDE'S BOARD, ON A REAL BUS.
//
// examples/boards/lamp/lamp.h is the card the guide teaches you to write, and this is what
// stops it from rotting. A tutorial whose code is a listing inside a Markdown file is a
// tutorial that compiled once, on the day it was written, against headers that have since
// moved. This one has to keep working or the suite goes red.
//
// It also pins down the guide's central CLAIM, which is not obvious and would be easy to
// get wrong: the front panel answers `IN 0FFH` and the lamp answers `OUT 0FFH`, on the same
// port, in the same machine, WITHOUT CONTENDING -- because the bus decodes each direction
// separately, exactly as the real backplane has separate strobes. If that ever stops being
// true, the chapter is teaching a falsehood, and this test is the thing that says so.

#include "test.h"

#include "../examples/boards/lamp/lamp.h"
#include "boards/mits-frontpanel.h"
#include "core/machine.h"

using namespace altair;

void test_lamp() {
    SECTION("the lamp card -- the Developer Guide's board, and the bus routing it relies on");

    {
        Machine m;
        auto* lamp = new LampBoard();
        lamp->id = "lamp0";
        m.bus.attach(lamp);

        // Nothing has been written, and a card comes up dark.
        CHECK(lamp->properties()[1].get().i() == 0, "the lamps start out");

        // A REAL OUT CYCLE on the bus -- not a method call on the board. This is the same
        // path the 8080's OUT instruction takes, and the same one the monitor's `OUT`
        // command takes, because there is only one.
        m.bus.ioWrite(0xFF, 0x55);
        CHECK(lamp->properties()[1].get().i() == 0x55, "OUT FF latches the byte");

        m.bus.ioWrite(0xFF, 0xAA);
        CHECK(lamp->properties()[1].get().i() == 0xAA, "...and the next one replaces it");

        // The card is WRITE-ONLY. It must not answer a read, and the proof that it does not
        // is that the read floats: nobody drove the bus, so it reads FF.
        CHECK(m.bus.ioRead(0xFF) == 0xFF, "an IN from FF is not the lamp's -- the bus floats");

        // RESET* puts the lamps out. So does the power coming up.
        //
        // Called on the BOARD, not through Machine::reset() -- and the distinction is worth
        // a sentence, because it caught this test out. bus.attach() wires a card to the
        // backplane; it does not hand it to the MACHINE. Lifecycle events (reset, power,
        // pump) are dispatched by Machine over the boards it OWNS, so a board that is only
        // on the bus answers cycles and never hears the RESET line. A card that gets its id
        // from a machine file goes in through Machine::add() and gets both.
        lamp->reset(Reset::Bus);
        CHECK(lamp->properties()[1].get().i() == 0, "RESET* clears the latch");

        m.bus.ioWrite(0xFF, 0x33);
        lamp->power();
        CHECK(lamp->properties()[1].get().i() == 0, "...and so does the power coming up");
    }

    {
        // THE CLAIM THE CHAPTER IS BUILT ON.
        //
        // Both cards on port FF, in one machine. The panel drives the IN; the lamp drives
        // the OUT; neither one is in the other's way. A board that decoded on the address
        // alone -- which is the obvious way to write decodes(), and the wrong one -- would
        // have collided here, and the SENSE switches would have stopped working.
        Machine m;
        auto* fp = new FrontPanelBoard();
        fp->id = "fp0";
        auto* lamp = new LampBoard();
        lamp->id = "lamp0";
        m.bus.attach(fp);
        m.bus.attach(lamp);
        m.power();

        std::string err;
        CHECK(setProperty(*fp, "sense", "3C", err), "set the SENSE switches");

        // The panel still answers the IN...
        CHECK(m.bus.ioRead(0xFF) == 0x3C, "IN FF is still the front panel's SENSE switches");

        // ...and the lamp still answers the OUT.
        m.bus.ioWrite(0xFF, 0x81);
        CHECK(lamp->properties()[1].get().i() == 0x81, "OUT FF is the lamp's");
        CHECK(m.bus.ioRead(0xFF) == 0x3C, "and the OUT did not disturb what the panel reads");

        // AND EXACTLY ONE CARD DRIVES EACH DIRECTION. This is the assertion the whole
        // chapter rests on. respondersTo() is what WHO and the contention detector are
        // built from, so if it ever returned both boards for one cycle, the machine would
        // be reporting a hardware fault at the reader's very first example.
        BusCycle in{Cycle::IoRead, 0xFF};
        BusCycle out{Cycle::IoWrite, 0xFF};
        auto readers = m.bus.respondersTo(in);
        auto writers = m.bus.respondersTo(out);

        CHECK(readers.size() == 1 && readers[0] == (Board*)fp,
              "IN FF: the front panel, and ONLY the front panel");
        CHECK(writers.size() == 1 && writers[0] == (Board*)lamp,
              "OUT FF: the lamp, and ONLY the lamp");
        CHECK(m.bus.drain().empty(),
              "one port, two cards, opposite directions -- and the bus says NOTHING, "
              "because it is not contention");
    }

    {
        // The port is a property, so it moves -- and the decode moves with it, because the
        // property layer calls decodeChanged() after every successful set. A board author
        // who had to remember that call would eventually forget it, and the symptom would
        // be a card that answers at its old address until something else happened to
        // invalidate the bus's cache.
        Machine m;
        auto* lamp = new LampBoard();
        lamp->id = "lamp0";
        m.bus.attach(lamp);

        std::string err;
        CHECK(setProperty(*lamp, "port", "FE", err), "move the card to port FE");

        m.bus.ioWrite(0xFE, 0x77);
        CHECK(lamp->properties()[1].get().i() == 0x77, "it answers at its new port");

        m.bus.ioWrite(0xFF, 0x11);
        CHECK(lamp->properties()[1].get().i() == 0x77,
              "...and NOT at its old one -- the bus's decode cache was invalidated");
    }
}
