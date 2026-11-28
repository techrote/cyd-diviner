#include "../src/instrument_touch.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace instrument_touch;
    // Landscape/portrait and hidden/shown dock geometry. Rotation is already
    // applied by the HAL: the same cases cover the two 180-degree orientations.
    const Viewport cases[] = {
        {0, 0, 320, 180, 180}, {0, 30, 320, 180, 240},
        {0, 47, 240, 135, 230}, {0, 92, 240, 135, 320},
        {0, 0, 320, 180, 110}, {0, 17, 240, 135, 170},
    };
    unsigned tested = 0;
    for (const auto &v : cases) {
        assert(!contains(v, -1, v.top) && !contains(v, v.width, v.top));
        assert(!contains(v, 0, v.top - 1) && !contains(v, 0, v.bottom));
        for (int y = v.top; y < v.top + v.height && y < v.bottom; ++y) {
            int previous = -1;
            for (int x = 0; x < v.width; ++x) {
                assert(contains(v, x, y));
                auto p = map(v, x, y, true, true, 123);
                assert(p.x >= previous && p.x >= 0 && p.x < 240 && p.y >= 0 && p.y < 135);
                assert(p.start && p.down && p.ms == 123);
                previous = p.x;
                ++tested;
            }
        }
        auto p = map(v, -500, -500, false, true, 456);
        assert(p.x == 0 && p.y == 0);
        p = map(v, 5000, 5000, false, true, 789);
        assert(p.x == 239 && p.y == 134);
    }
    assert(!contains({0,0,0,0,0}, 0, 0));
    std::printf("PASS CYD viewport: %u visible pixels, clipping, edge clamps, monotonic logical coordinates\n", tested);
}
