#include "core/machines.h"

#include "config/toml.h"

#include <cctype>

namespace altair {

const BuiltinMachine* findMachine(const std::string& name) {
    std::string want;
    for (char c : name) want += (char)std::tolower((unsigned char)c);
    for (const auto& b : builtinMachines())
        if (want == b.name) return &b;
    return nullptr;
}

bool loadMachine(const BuiltinMachine& b, Machine& m, std::string& err) {
    return loadTomlText(std::string(b.toml, b.size), std::string("builtin:") + b.name, m, err);
}

bool looksLikeFile(const std::string& arg) {
    if (arg.find('/') != std::string::npos) return true;
    if (arg.find('\\') != std::string::npos) return true;
    if (arg.size() >= 5) {
        std::string tail;
        for (size_t i = arg.size() - 5; i < arg.size(); ++i)
            tail += (char)std::tolower((unsigned char)arg[i]);
        if (tail == ".toml") return true;
    }
    return false;
}

} // namespace altair
