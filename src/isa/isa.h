#pragma once
//
// The instruction set -- a STATELESS disassembler (DESIGN.md 3.0.2).
//
// Bytes in, text and a length out. No registers, no bus, no board, no CPU.
//
// This is a layer of its own because "CPU" is three things wearing one name:
//
//   instruction set   how bytes decode            <- THIS FILE
//   core              registers + execute         src/cpu/
//   card              the thing you pull out      src/boards/
//
// Two 8080 cards that differ only in an onboard serial port share the
// instruction set and the core COMPLETELY, and differ only in the card -- which
// is the only place they differ in reality (Patrick, 2026-07-11). The thing they
// have in common is not the chip and not the board: it is the way bytes decode,
// and that is why it needs a name of its own to be shared by.
//
// AND IT RUNS WITH NO CPU IN THE MACHINE. `DISASM FF00 CPU=8080` worked in
// milestone 1a, against the DBL PROM, before a single instruction could execute
// -- which is the same argument that made the bus testable before the CPU
// existed, and it means the decode tables get exercised long before anything
// runs them.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

// One decoded instruction. `len` is 1..3 for the 8080; a caller stepping through
// memory adds it to the address and asks again.
struct Insn {
    std::string text;      // "LXI H,FF13"
    uint8_t len = 1;
    bool undocumented = false;  // 08, CB, D9... -- real silicon runs these
};

// Read a byte at an address. The disassembler is handed one of these rather than
// a Bus, because it has no business running a bus cycle: a DISASM must not
// consume a byte from a UART or trip a card's snoop latch. The monitor passes a
// non-invasive peek; a test passes a lambda over an array.
using PeekFn = std::function<uint8_t(uint16_t)>;

class Disassembler {
public:
    virtual ~Disassembler() = default;
    virtual const char* name() const = 0;   // "8080" -- the registry key
    virtual Insn at(uint16_t addr, const PeekFn& peek) const = 0;
};

// Null if we do not speak that instruction set. The caller reports it; this does
// not guess and does not fall back to the 8080 -- silently disassembling a Z80
// as an 8080 produces plausible, wrong text, which is worse than an error.
const Disassembler* disassemblerFor(const std::string& isa);

// Every instruction set we know, for tab completion and the error message.
std::vector<std::string> instructionSets();

} // namespace altair
