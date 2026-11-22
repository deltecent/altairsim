#include "test.h"

#include "host/volume.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

void test_volume() {
    SECTION("Volume: 0 dB passes a sample below the knee as it is");
    {
        Volume v;
        bool same = true;
        for (int x = -29000; x <= 29000; x += 7) same = same && v((double)x) == x;
        CHECK(same, "every sample from -29000 to +29000 is unchanged");
        std::vector<int16_t> s = {0, 100, -100, 25000, -32768};
        v.apply(s);
        CHECK(s[0] == 0 && s[1] == 100 && s[2] == -100 && s[3] == 25000, "apply() too");
        CHECK(s[4] > -32767 && s[4] < -29490, "-32768 is past the knee, and bends towards it");
    }

    SECTION("Volume: the gain in dB");
    {
        Volume up, down;
        CHECK(up.set(6) && up.db() == 6, "+6 dB is taken");
        CHECK(down.set(-6) && down.db() == -6, "-6 dB is taken");
        CHECK(std::abs(up(1000) - 1995) <= 1, "+6 dB is about twice");
        CHECK(std::abs(down(1000) - 501) <= 1, "-6 dB is about half");
        CHECK(up(-1000) == -up(1000), "the same either side of zero");
    }

    SECTION("Volume: a peak past full scale is rounded off, not clipped");
    {
        Volume v;
        v.set(Volume::kMaxDb);
        int  last      = -32768;
        bool monotonic = true, inside = true;
        for (double x = 0; x <= 4 * 32767.0 / 15.85; x += 3) {   // up to 4x full scale
            const int y = v(x);
            monotonic = monotonic && y >= last;
            inside    = inside && y <= 32767 && v(-x) >= -32767;
            last      = y;
        }
        CHECK(monotonic, "a bigger sample never comes out smaller");
        CHECK(inside, "never past +-32767");

        Volume flat;
        CHECK(flat(1.2 * 32767) < flat(2.0 * 32767), "1.2x and 2x full scale come out different");
        CHECK(flat(1.0 * 32767) > 0.9 * 32767 && flat(1.0 * 32767) < 32767,
              "full scale itself bends below full scale");
        CHECK(flat(100.0 * 32767) == 32767, "and nothing, however big, passes it");
    }

    SECTION("Volume: the range, and the property");
    {
        Volume v;
        CHECK(!v.set(Volume::kMinDb - 1) && v.db() == 0, "-41 dB is refused, nothing changes");
        CHECK(!v.set(Volume::kMaxDb + 1) && v.db() == 0, "+25 dB is refused");
        CHECK(v.set(Volume::kMinDb) && v.set(Volume::kMaxDb), "both ends are taken");

        Volume w;
        int    calls = 0;
        Property p = volumeProperty(w, [&] { ++calls; });
        std::string err;
        CHECK(p.name == "volume" && p.unit == "dB", "named volume, in dB");
        CHECK(p.set(Value::ofInt(-12), err) && w.db() == -12 && calls == 1,
              "a good value is stored, and the board is told");
        CHECK(!p.set(Value::ofInt(25), err) && w.db() == -12 && calls == 1 && !err.empty(),
              "a bad one is refused with a reason, and nothing changes");
        CHECK(p.get().i() == -12, "get() reads it back");
    }
}
