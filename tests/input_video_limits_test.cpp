#include "input_video_limits.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using mkvc::input_limits::valid_dimensions;
    assert(valid_dimensions(1, 1));
    assert(valid_dimensions(16384, 16384));
    assert(!valid_dimensions(0, 1));
    assert(!valid_dimensions(1, -1));
    assert(!valid_dimensions(32769, 2));
    assert(!valid_dimensions(2, 32769));
    assert(!valid_dimensions(32768, 32768));
    assert(!valid_dimensions(INT64_MAX, 2));
    return 0;
}
