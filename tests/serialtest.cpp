// THE SERIAL LINE, ON REAL HARDWARE.
//
// Two USB serial ports with a NULL MODEM between them. Everything else in this
// project is provable with a ScriptedStream and a MemoryMedia, and should be --
// but "does RTS on this card actually raise CTS on that one" is a question about a
// CABLE, and the only honest way to answer it is to put a volt down one and look.
//
//   ALTAIR_SERIAL_A=/dev/tty.usbserial-AL009KFH \
//   ALTAIR_SERIAL_B=/dev/tty.usbserial-AB0NW409 ctest -L hw
//
// Unset, it SKIPS, and says so, and exits 77 so that CTEST SAYS SO TOO (see the
// SKIP_RETURN_CODE property in CMakeLists.txt). A hardware test that quietly passes
// when the hardware is absent is worse than not having one -- it is a green tick
// that means nothing, which is the only kind of test result that can lie to you.
// Printing "SKIPPED" and then returning 0 is that same lie in a smaller font: the
// message scrolls past and the summary line still reads `Passed`.
//
// WHAT A NULL MODEM CROSSES (and it is the whole reason this test can exist):
//
//     A TxD  -> B RxD          A RTS -> B CTS
//     A RxD  <- B TxD          A CTS <- B RTS
//     A DTR  -> B DSR + B DCD  (both, from the one pin)
//     A DSR + A DCD <- B DTR
//
// So B raising DTR is a CARRIER APPEARING at A. That is not an analogy -- to the
// 6850 on the card, with `dcd=wired`, it is indistinguishable from a modem.

#include "test.h"

#include "boards/mits-2sio.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "platform/serial.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

using namespace altair;

int g_fail = 0;
int g_run  = 0;

namespace {

// A REAL WIRE TAKES REAL TIME, and this is the one place in the project that has to
// admit it. Elsewhere time is a T-state count and the test is instantaneous; here a
// byte is a physical thing crossing a cable at 9600 baud, and a modem pin takes a
// USB round trip to move. So: poll, briefly, and give up with a message rather than
// hang.
template <typename Fn>
bool waitFor(Fn ready, int ms = 500) {
    for (int i = 0; i < ms; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ready();
}

// LET THE LINE SETTLE, THEN THROW AWAY WHAT THE WIGGLING PRODUCED.
//
// Slamming RTS and DTR around on a null modem can put a transient on the far end's
// RxD -- a spurious start bit, and therefore a garbage character. That is NOT a bug
// to fix in the simulator: it is what the cable really does, and a real guest would
// really see it. It is a bug to have in a BENCH, though, which is what this is. You
// let the line settle before you trust it, exactly as you would with a scope probe.
//
// (This is a hypothesis, not a diagnosis. This test failed ONCE, on the run
// immediately after a rebuild, and has not failed in 58 runs since -- so the fault
// was never caught in the act. What is written down here is the one mechanism that
// could produce it, and this removes that mechanism. If it ever fails again, the
// first thing to know is WHICH check failed, so do not pipe this to `tail`.)
void settle(platform::SerialPort& a, platform::SerialPort& b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint8_t junk[64];
    while (a.read(junk, sizeof junk)) {}
    while (b.read(junk, sizeof junk)) {}
}

// The exit code CMakeLists.txt hands to ctest as SKIP_RETURN_CODE. 77 is the
// automake convention, and ctest has no opinion of its own -- any code the two
// files agree on would do.
constexpr int kSkip = 77;

} // namespace

int main() {
    const char* pathA = std::getenv("ALTAIR_SERIAL_A");
    const char* pathB = std::getenv("ALTAIR_SERIAL_B");

    if (!pathA || !pathB) {
        std::printf(
            "SKIPPED: no serial hardware.\n"
            "  This test needs TWO serial ports with a null modem between them:\n"
            "    ALTAIR_SERIAL_A=/dev/tty.usbserial-XXXX \\\n"
            "    ALTAIR_SERIAL_B=/dev/tty.usbserial-YYYY ctest -L hw\n");
        return kSkip;
    }

    std::printf("A = %s\nB = %s\n", pathA, pathB);

    // -----------------------------------------------------------------------
    SECTION("The platform layer -- bytes and pins across a real cable");
    // -----------------------------------------------------------------------
    platform::SerialConfig cfg;
    cfg.baud = 9600;

    std::string err;
    auto a = platform::openSerialPort(pathA, cfg, err);
    CHECK(a != nullptr, ("port A opens: " + err).c_str());
    auto b = platform::openSerialPort(pathB, cfg, err);
    CHECK(b != nullptr, ("port B opens: " + err).c_str());

    if (!a || !b) {
        std::printf("\n%d checks, %d failed\n", g_run, g_fail);
        return 1;
    }

    // Start from a known state: both ends driving nothing -- then let the line settle
    // and discard whatever the pin-wiggling put on it. See settle().
    a->setControl(false, false);
    b->setControl(false, false);
    settle(*a, *b);

    // ---- Bytes ----
    const uint8_t msg[] = "ALTAIR";
    a->write(msg, 6);
    a->flush();

    std::string got;
    waitFor([&] {
        uint8_t buf[64];
        got.append((const char*)buf, b->read(buf, sizeof buf));
        return got.size() >= 6;
    });
    CHECK(got == "ALTAIR", ("A -> B carries the bytes (got '" + got + "')").c_str());

    // ---- Pins: RTS on A is CTS on B ----
    a->setControl(/*rts=*/true, /*dtr=*/false);
    CHECK(waitFor([&] { return b->lines().cts; }), "A raises RTS -> B sees CTS");
    CHECK(!b->lines().carrier, "...and DCD is a DIFFERENT wire, so it has not moved");

    a->setControl(/*rts=*/false, /*dtr=*/false);
    CHECK(waitFor([&] { return !b->lines().cts; }), "A drops RTS -> B's CTS goes away");

    // ---- Pins: DTR on A is DCD *and* DSR on B ----
    a->setControl(/*rts=*/false, /*dtr=*/true);
    CHECK(waitFor([&] { return b->lines().carrier; }), "A raises DTR -> B sees CARRIER");
    CHECK(b->lines().dsr, "...and DSR, off the same pin");

    a->setControl(false, false);
    CHECK(waitFor([&] { return !b->lines().carrier; }), "A drops DTR -> B's carrier drops");

    // Hand the cable back before the emulator opens it.
    a.reset();
    b.reset();

    // -----------------------------------------------------------------------
    SECTION("A 6850 in a machine, with a real cable on it");
    //
    // THIS IS THE ONE THAT MATTERS. Everything above proves the platform layer; this
    // proves the CHIP -- that a card in a backplane, strapped `cts=wired`, actually
    // stops transmitting when a physical pin on a physical cable goes low, and that a
    // physical carrier drop latches a status bit and pulls pin 73.
    // -----------------------------------------------------------------------
    Sio2Board::setResolver(resolveEndpoint);

    Machine m;
    m.bus.setVerify(true);  // as every board suite does: it is what catches a missing intChanged()

    auto* sio = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));
    CHECK(sio != nullptr, "a 2SIO in the backplane");
    m.add("8080", "cpu0", err);
    m.power();

    CHECK(sio->connect("a", std::string("serial:") + pathA, err),
          ("CONNECT sio0:a serial:A -- " + err).c_str());

    // The OTHER end of the null modem, driven by hand. This is the terminal, the
    // modem, the person at the far end of the phone line.
    auto far = platform::openSerialPort(pathB, cfg, err);
    CHECK(far != nullptr, ("the far end opens: " + err).c_str());

    if (sio && far) {
        Mc6850* ch = sio->channel("a");

        // Both straps WIRED: this card believes the cable. (The default is `ground`,
        // which is the period default and the reason every existing config still
        // works -- but a grounded pin is exactly the pin that cannot be tested.)
        ch->dcdStrap = PinStrap::Wired;
        ch->ctsStrap = PinStrap::Wired;

        // The far end asserts everything: carrier up, clear to send.
        far->setControl(/*rts=*/true, /*dtr=*/true);
        CHECK(waitFor([&] {
            m.pump();
            return ch->carrier() && ch->clearToSend();
        }), "the far end raises RTS and DTR -> the card has carrier and CTS");

        // 8N1, receive interrupt enabled, and the interrupt strapped to pin 73.
        ch->jumper = IrqJumper::Int;
        m.bus.ioWrite(0x10, 0x03);  // master reset -- ALTMON's first instruction
        m.bus.ioWrite(0x10, 0x95);  // 8N1 + RIE

        // The card just asserted RTS (control bits 5-6 = 00) and that is a real
        // transition on a real wire. Let it settle and drop whatever it produced,
        // for the reason settle() gives.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint8_t junk[64];
        while (far->read(junk, sizeof junk)) {}

        // ---- The guest transmits, and it comes out of the cable ----
        m.bus.ioWrite(0x11, 'H');
        m.bus.ioWrite(0x11, 'I');
        m.pump();

        std::string heard;
        waitFor([&] {
            uint8_t buf[64];
            heard.append((const char*)buf, far->read(buf, sizeof buf));
            return heard.size() >= 2;
        });
        CHECK(heard == "HI", ("the guest's OUT reaches the wire (got '" + heard + "')").c_str());

        // ---- The far end types, and the guest receives -- and INTERRUPTS ----
        const uint8_t k[] = {'K'};
        far->write(k, 1);
        far->flush();

        CHECK(waitFor([&] {
            m.pump();
            m.clock.advance(5000);  // let the character finish arriving, at 9600 baud
            return (m.bus.ioRead(0x10) & 0x01) != 0;
        }), "a byte typed on the cable raises RDRF");
        CHECK(m.bus.intPending(), "...and PIN 73, because the interrupt is jumpered");
        CHECK(m.bus.ioRead(0x11) == 'K', "...and it is the byte that was typed");

        // ---- /CTS: THE FAR END SAYS STOP, AND THE TRANSMITTER STOPS ----
        //
        // This is real flow control over a real wire. The data sheet: "In the high
        // state, the Transmit Data Register Empty bit is inhibited."
        m.clock.advance(50000);  // any character in flight is long gone
        CHECK((m.bus.ioRead(0x10) & 0x02) != 0, "TDRE set while the far end is clear to send");

        far->setControl(/*rts=*/false, /*dtr=*/true);  // drop RTS -> the card's CTS goes low
        CHECK(waitFor([&] {
            m.pump();
            return (m.bus.ioRead(0x10) & 0x02) == 0;
        }), "the far end drops RTS -> TDRE is INHIBITED and the guest must wait");
        CHECK((m.bus.ioRead(0x10) & 0x08) != 0, "...and status bit 3 says so (/CTS negated)");

        far->setControl(/*rts=*/true, /*dtr=*/true);
        CHECK(waitFor([&] {
            m.pump();
            return (m.bus.ioRead(0x10) & 0x02) != 0;
        }), "RTS back -> the transmitter is free again");

        // ---- /DCD: A REAL CARRIER DROP, LATCHED, AND IT INTERRUPTS ----
        far->setControl(/*rts=*/true, /*dtr=*/false);  // DTR down = carrier gone
        CHECK(waitFor([&] {
            m.pump();
            return (m.bus.ioRead(0x10) & 0x04) != 0;
        }), "the far end drops DTR -> DCD, and the card LATCHES it");
        CHECK(m.bus.intPending(), "...and interrupts: a carrier loss is a receive interrupt");

        // The carrier comes back, and the bit does NOT clear by itself.
        far->setControl(/*rts=*/true, /*dtr=*/true);
        waitFor([&] {
            m.pump();
            return ch->carrier();
        });
        CHECK((m.bus.ioRead(0x10) & 0x04) != 0, "carrier back, bit STILL SET -- it is a latch");

        // Status, then data. That is the sequence, and only that sequence.
        (void)m.bus.ioRead(0x10);
        (void)m.bus.ioRead(0x11);
        m.pump();
        CHECK((m.bus.ioRead(0x10) & 0x04) == 0, "status-then-data clears it");

        far->setControl(false, false);
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
