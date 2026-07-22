#pragma once
//
// StateWriter / StateReader -- the byte-exact serialization primitive behind
// SNAPSHOT and RESTORE (DESIGN.md 13).
//
// EXPLICIT, LITTLE-ENDIAN, VERSIONED, and never a memcpy of a struct (DESIGN.md
// 7.0). A snapshot must read back BYTE-FOR-BYTE IDENTICAL on all four targets,
// so every integer is written low byte first, by shifts, and every
// variable-length thing carries its own length. A struct's padding and the
// host's byte order are exactly the two things this refuses to depend on -- and
// they are the two things a memcpy would quietly bake in.
//
// NO EXCEPTIONS. The reader latches an error on the first underrun and every
// read after it is a no-op returning 0, so a truncated or corrupt file cannot
// read past its end and cannot half-apply: the caller checks ok() once, at the
// end. That is the same shape as StepResult (DESIGN.md 16.3) -- an explicit
// result, never a sentinel that a caller can mistake for real data.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// APPEND-ONLY. A board serializes into its own StateWriter and the machine
// length-prefixes the result (Machine::snapshot), so nothing here seeks.
class StateWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void boolean(bool v) { u8(v ? 1 : 0); }

    // A length-prefixed string (u32 count + bytes). For an id, a type, a path.
    void str(const std::string& s);

    // RAW bytes, no length of their own -- the caller knows the count on both
    // sides (a fixed array: a 1K screen, a 137-byte sector, 16 switches).
    void raw(const uint8_t* p, size_t n);

    // A length-prefixed byte vector (u32 count + bytes) -- for a store whose
    // size the reader does not otherwise know.
    void blob(const std::vector<uint8_t>& v);

    const std::vector<uint8_t>& data() const { return buf_; }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

class StateReader {
public:
    StateReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    explicit StateReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}

    uint8_t  u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    bool     boolean() { return u8() != 0; }
    std::string str();
    void        raw(uint8_t* out, size_t n);
    std::vector<uint8_t> blob();

    // False the moment a read ran off the end. Once false it stays false, and
    // every read returns 0 -- so a caller may parse a whole structure and check
    // this once, and a corrupt file cannot be half-applied.
    bool ok() const { return ok_; }
    const std::string& error() const { return err_; }
    size_t remaining() const { return ok_ ? n_ - pos_ : 0; }

private:
    // Returns true and advances only if n bytes are actually there; otherwise
    // latches the error and returns false. Every read goes through here.
    bool want(size_t n);

    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
    std::string err_;
};

} // namespace altair
