#pragma once
//
// TnfsMedia -- a MediaFile that lives on a TNFS server (DESIGN.md 7.7).
//
// TNFS (The Network File System) is the FujiNet protocol: a small request/reply
// filesystem over UDP, port 16384 by default. This makes a disk or tape image on such
// a server mountable exactly like a local file --
//
//     MOUNT dcdd0:drive0 tnfs://myserver/games/cpm.dsk
//
// and every board gets it for free, because the whole difference from a local file is
// WHERE THE BYTES ARE. A TnfsMedia is a HostFile with a network under it: the image is
// slurped once at MOUNT into RAM, read/written there during emulation, and the dirty
// range is pushed back over TNFS at sync(). That is the same buffered, dirty-write-back
// model media.h documents, and it is what keeps the network OFF the emulation path --
// the guest never blocks on a datagram to read a sector, only (as with any medium) on
// sync, which for a disk is one small write per sector.
//
// SCOPE: slurp-sized single-file images (floppies, tapes, hard-sector, 88-HDSK). A
// multi-hundred-megabyte CF/SD card (the lazy CardImage model) is deliberately NOT
// served this way -- a network round trip per sector inside a bus cycle would stall
// emulated time. openHostMedia refuses those before they reach here.

#include "host/media.h"
#include "platform/socket.h"

#include <functional>
#include <string>

namespace altair {

// THE TEST SEAM, same shape as media.h's MediaResolver. TnfsMedia gets its UDP socket
// through this, which defaults to platform::connectUdp -- the real network. A test
// installs its own connector returning a fake UdpSocket that answers as a server would,
// so the whole protocol (mount, slurp, dirty write-back, retransmit, EAGAIN) is exercised
// with no real port, no thread, and nothing to flake. Pass nullptr to restore the default.
using UdpConnector =
    std::function<std::unique_ptr<platform::UdpSocket>(const std::string& host, uint16_t port,
                                                       std::string& err)>;
void setTnfsUdpConnector(UdpConnector c);

// tnfs://<host>[:<port>]/<path>. Null and `err` set on anything that will not open --
// a bad URL, a name that will not resolve, a server that never answers, or a file the
// server does not have. `readOnly` is the operator's WP; a server that refuses write
// (EROFS/EACCES) also mounts read-only, and readOnlyForced() then says so.
std::unique_ptr<MediaFile> openTnfsMedia(const std::string& url, bool readOnly, std::string& err);

// Is this a TNFS URL? The one place openHostMedia decides to route here. Case-sensitive
// on the scheme, as URL schemes and the existing endpoint prefixes are.
bool isTnfsUrl(const std::string& path);

} // namespace altair
