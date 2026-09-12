#include "container_ebml.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    mkvc::container_detail::HeaderInfo info;
    const std::vector<uint8_t> valid = {0x1A, 0x45, 0xDF, 0xA3, 0x85, 0x42, 0x82, 0x82, 'm', 'k'};
    assert(mkvc::container_detail::parse_header_bytes(valid, info));
    assert(info.doc_type == "mk");

    // The element-size VINT starts inside the declared EBML payload but ends
    // outside it. Bytes beyond the payload must not make the subtraction of
    // header_end - position wrap and turn an invalid size into a huge string.
    const std::vector<uint8_t> crossing_size = {0x1A, 0x45, 0xDF, 0xA3, 0x83, 0x42, 0x82, 0x01,
                                                0x00, 0x00, 0x00, 0xBD, 0x00, 0x00, 0x00, 0x00};
    assert(!mkvc::container_detail::parse_header_bytes(crossing_size, info));
    return 0;
}
