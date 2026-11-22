#include "host/volume.h"

#include <cmath>
#include <string>

namespace altair {
namespace {

constexpr double kFull = 32767.0;
constexpr double kKnee = 0.9 * kFull;

} // namespace

bool Volume::set(int db) {
    if (db < kMinDb || db > kMaxDb) return false;
    db_   = db;
    gain_ = std::pow(10.0, db / 20.0);
    return true;
}

int16_t Volume::operator()(double x) const {
    double y = x * gain_;
    const double a = std::fabs(y);
    if (a > kKnee) {
        const double room = kFull - kKnee;
        y = std::copysign(kKnee + room * std::tanh((a - kKnee) / room), y);
    }
    // tanh() stays below 1, so y is inside +-32767; the clamp only guards the rounding.
    const double r = std::round(y);
    return (int16_t)(r > kFull ? kFull : (r < -kFull ? -kFull : r));
}

void Volume::apply(std::span<int16_t> samples) const {
    for (int16_t& s : samples) s = (*this)((double)s);
}

Property volumeProperty(Volume& v, std::function<void()> changed) {
    Property x;
    x.name = "volume";
    x.help = "How loud the board's sound is on this computer, in dB: " +
             std::to_string(Volume::kMinDb) + " to +" + std::to_string(Volume::kMaxDb) +
             ". A host setting; the board does not change. A peak that would pass full "
             "scale is rounded off. Default 0";
    x.kind = Kind::Int;
    x.min  = Volume::kMinDb;
    x.max  = Volume::kMaxDb;
    x.unit = "dB";
    x.get  = [&v] { return Value::ofInt(v.db()); };
    x.set  = [&v, changed](const Value& val, std::string& err) {
        if (!v.set((int)val.i())) {
            err = "the volume is " + std::to_string(Volume::kMinDb) + " to +" +
                  std::to_string(Volume::kMaxDb) + " dB";
            return false;
        }
        if (changed) changed();
        return true;
    };
    return x;
}

} // namespace altair
