#include "host/endpoint.h"

#include "host/console.h"

namespace altair {

std::string endpointHelp() {
    return "console | null | loopback  (socket: and serial: are not built yet)";
}

std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err) {
    if (spec == "console") return std::make_unique<ConsoleRef>();
    if (spec == "null") return std::make_unique<NullStream>();
    if (spec == "loopback") return std::make_unique<LoopbackStream>();

    // NAME WHAT IS COMING, and say plainly that it is not here yet. A user who
    // types `socket:2323` has a specific expectation, and "unknown endpoint" would
    // leave them wondering whether they had the syntax wrong. They didn't.
    if (spec.rfind("socket:", 0) == 0) {
        err = "socket: endpoints are not implemented yet (milestone 1b). " + endpointHelp();
        return nullptr;
    }
    if (spec.rfind("serial:", 0) == 0) {
        err = "serial: endpoints are not implemented yet (milestone 1b). " + endpointHelp();
        return nullptr;
    }
    if (spec.rfind("file:", 0) == 0) {
        err = "file: endpoints are not implemented yet. " + endpointHelp();
        return nullptr;
    }

    err = "no endpoint '" + spec + "'. Try: " + endpointHelp();
    return nullptr;
}

} // namespace altair
