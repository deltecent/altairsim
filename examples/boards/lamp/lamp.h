#pragma once
//
// THE LAMP CARD -- the board the Developer Guide teaches you to write.
//
// Eight LEDs and a latch. `OUT 0FFH` lights them; that is the entire card. It is the
// smallest thing that is honestly a board: it decodes a cycle, it holds state, it has a
// setting, and it shows up in SHOW, in BOARDS, in a machine file and in CONFIG SAVE
// without a line of code anywhere else.
//
// IT IS NOT IN THE SHIPPING REGISTRY, AND THAT IS THE POINT.
//
// Every type in `BOARDS TYPES` is hardware somebody could buy in 1976 -- except the Host
// Bridge, which earns its place by doing real work and says in its own blurb that it is
// ours. A lamp latch does no work. It exists to be read about. Putting it in the registry
// would also spoil the tutorial's last step, which is "now add one line to
// src/boards/registry.cpp" -- a step the reader is meant to PERFORM, not to discover has
// already been done for them.
//
// So it lives here, out of the product, and is compiled into the TEST binary and driven by
// a real bus in tests/test_lamp.cpp. That is what stops the Developer Guide's code from
// rotting: it is not a listing in a Markdown file, it is a board that has to keep working.
//
// WHY PORT 0xFF, WHICH LOOKS TAKEN.
//
// The front panel already answers port 0xFF -- that is where the SENSE switches are. But it
// answers it for INPUT ONLY: on the real card the buffer's enable is gated with sINP, there
// is no sOUT anywhere near it, and an `OUT 0FFH` is simply not the panel's. The byte goes
// nowhere.
//
// The bus keeps a separate decode for each direction (Bus::ioRead_ and Bus::ioWrite_), for
// the same reason the real backplane has separate strobes. So this card and the front panel
// sit on the same port number and do not contend, and the machine will tell you so:
//
//     altairsim> WHO IO FF
//     port FF IN:  fp0    (the SENSE switches)
//     port FF OUT: lamp0  (lamp latch -- D0..D7)
//
// That is the lesson the card is really for. THE BUS ROUTES BY CYCLE TYPE, NOT JUST BY
// ADDRESS -- and a board that decodes on the address alone would have broken the panel.

#include "core/board.h"

namespace altair {

class LampBoard : public Board {
public:
    // The name a machine file calls this card. It is the CHIP or the common word, never a
    // catalog number -- nobody ever asked for an 88-CPU, they asked for an 8080.
    std::string type() const override { return "lamp"; }

    // ---- The bus ----------------------------------------------------------------
    //
    // decodes() answers ONE question: "if this cycle happened, would I drive the bus?"
    //
    // IT MUST BE PURE AND COMBINATIONAL. The bus caches the answer -- one slot per port,
    // per direction -- and asks again only when a board says its decode changed. A
    // decodes() with a side effect in it is a bug that will not show up for a month.
    bool decodes(const BusCycle& c) const override {
        if (!enabled_) return false;              // a card that is switched off drives nothing
        if (c.type != Cycle::IoWrite) return false;  // OUT only. The panel owns IN.
        return c.port() == port_;
    }

    // We claimed the cycle, so the byte is ours.
    void write(const BusCycle& c) override { latch_ = c.data; }

    // read() is not overridden. We never say yes to a read, so we are never asked one.

    // ---- Lifecycle --------------------------------------------------------------
    //
    // Two different events, and a card is entitled to treat them differently. RESET* is the
    // button on the panel; POC* is the power coming up. Here they happen to mean the same
    // thing -- the lamps go out -- but a memory card, for one, must not confuse them.
    void reset(Reset) override { latch_ = 0; }
    void power() override { latch_ = 0; }

    // ---- Reflection -------------------------------------------------------------
    //
    // THIS IS THE WHOLE CONFIGURATION LAYER. There is no schema file, no parser, and no
    // registration call. SET, SHOW, the TOML loader, CONFIG SAVE, the MCP tool schemas, tab
    // completion and the manual's generated board reference are all written once, against
    // this vector, and know nothing about any particular board.
    //
    // Which is why a `port` here is a `port = FF` in a machine file, for free, today.
    std::vector<Property> properties() override {
        std::vector<Property> p;
        {
            Property x;
            x.name = "port";
            x.help = "the port this card latches. Write-only -- an IN here is not ours";
            x.kind = Kind::Int;
            x.radix = 16;  // ON THE WIRE -> HEX. A port is a thing the 8080 can see.
            x.min = 0;
            x.max = 0xFF;
            x.get = [this] { return Value::ofInt(port_); };
            x.set = [this](const Value& v, std::string&) {
                port_ = (uint8_t)v.i();
                // No decodeChanged() call here: the property layer calls it for us after
                // any successful set, precisely so that a board author cannot forget.
                return true;
            };
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name = "lamps";
            x.help = "what the guest last wrote -- the eight LEDs";
            x.kind = Kind::Int;
            x.radix = 16;
            x.get = [this] { return Value::ofInt(latch_); };
            // NO SETTER. That is how a consumer knows this is something the card KNOWS and
            // not something you CHOSE: SHOW prints "(read-only)", CONFIG SAVE leaves it out
            // of the file, and the manual's reference marks it. A setter that always failed
            // would stop a SET and fool all three.
            p.push_back(std::move(x));
        }
        return p;
    }

    // ---- Introspection ----------------------------------------------------------
    //
    // What BOARDS, SHOW BUS IO and WHO print. It is documentation, not decode -- the bus
    // never consults it, and a card whose ioMap() disagreed with its decodes() would be
    // lying to the operator while working perfectly.
    std::vector<MapEntry> ioMap() const override {
        return {{port_, port_, "write", "lamp latch -- D0..D7"}};
    }

private:
    uint8_t port_ = 0xFF;
    uint8_t latch_ = 0;
};

}  // namespace altair
