#pragma once
//
// TerminalEmulator -- the DIALECT half of a terminal: a state machine that turns an
// incoming byte stream into operations on a TerminalScreen (host/terminal/screen.h).
//
// This is where terminals differ. The SD Systems VDB-8024 answers its v1.6 firmware's
// control codes (SdVdb16Emulator, src/boards/sd-vdb8024.cpp); a VT100, an ADM-3A, a
// VT52 and a Heath/Zenith H19 each answer their own -- and each will be a subclass here
// (issue #244). The screen and the renderer are shared; only this changes.
//
// feed() takes ONE display byte and applies its effect to the screen. reset() returns
// the parser to its ground state (a mid-sequence ESC is abandoned), as an S-100 RESET
// or a power cycle does.
//
// AND IT REPLIES. A terminal is bidirectional: some sequences ask a question the guest
// reads back (a VT100's ESC[6n cursor-position report), and a host keystroke turns into
// the guest's own byte sequence (an arrow key is ESC[A on a VT100, ESC A on a VT52,
// Ctrl-K on an ADM-3A). Both are bytes flowing TOWARD the guest, so both land in one
// FIFO -- the reply buffer -- which whoever holds the terminal (the TerminalStream, an
// endpoint) drains with takeReply() and hands to the UART's receive path. feed() enqueues
// a report; keyAscii()/keySpecial() enqueue a keystroke; the guest cannot tell them apart,
// which is exactly right -- to a UART they are all just characters arriving on the line.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace altair {

class TerminalScreen;

class TerminalEmulator {
public:
    virtual ~TerminalEmulator() = default;

    // Apply one display byte to the screen, advancing any multi-byte sequence. May
    // enqueue a reply (a status report the guest asked for) via emit().
    virtual void feed(uint8_t b, TerminalScreen& scr) = 0;

    // Abandon any partial sequence and return to the ground state. An emulator that
    // emits should clearReply() here, because a RESET drops anything in flight.
    virtual void reset() = 0;

    // ---- host keyboard -> guest bytes ----
    //
    // The no-ASCII keys a windowed host captures. Each emulator encodes its own -- the
    // arrows are the classic divergence -- so key ROUTING (which window's keys reach which
    // line, the multi-window work) stays separate from key ENCODING, which is here.
    enum class Key { Up, Down, Left, Right, Home };

    // A printable/control key with an ASCII code passes straight through by default (most
    // terminals send the byte as-is); an emulator overrides only if it remaps something.
    virtual void keyAscii(uint8_t b) { emit(b); }

    // A no-ASCII key. The base sends nothing (a terminal with no such key), so an
    // emulator that has arrows overrides this. Enqueued onto the reply FIFO.
    virtual void keySpecial(Key) {}

    // ---- the reply FIFO (toward the guest) ----
    size_t takeReply(uint8_t* buf, size_t n) {
        size_t got = 0;
        while (got < n && replyHead_ < reply_.size()) buf[got++] = reply_[replyHead_++];
        if (replyHead_ >= reply_.size()) clearReply();  // fully drained -> reclaim
        return got;
    }
    bool hasReply() const { return replyHead_ < reply_.size(); }

protected:
    void emit(uint8_t b) { reply_.push_back(b); }
    void emit(const char* s) {
        while (*s) reply_.push_back((uint8_t)*s++);
    }
    void clearReply() {
        reply_.clear();
        replyHead_ = 0;
    }

private:
    std::vector<uint8_t> reply_;  // bytes owed to the guest: reports + encoded keys
    size_t               replyHead_ = 0;
};

} // namespace altair
