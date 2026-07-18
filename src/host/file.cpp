#include "host/file.h"

namespace altair {

// ALWAYS all of it. Like every ByteStream, write() cannot say "I took two of three"
// -- 7.1 forbids a board manufacturing a data loss the transport does not have. A
// file's write does not short: the OS buffers it. So we hand the whole run to the
// stream and report it consumed.
size_t FileStream::write(const uint8_t* buf, size_t n) {
    out_.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(n));
    return n;
}

} // namespace altair
