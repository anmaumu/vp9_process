#include "container_ebml.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

/** libFuzzer entry point for the bounded EBML header parser. */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Production reads at most 4096 bytes. Keeping the same bound prevents the
    // fuzz harness from testing allocations that callers cannot request.
    if (size > 4096) return 0;
    std::vector<uint8_t> bytes(data, data + size);
    mkvc::container_detail::HeaderInfo info;
    if (mkvc::container_detail::parse_header_bytes(bytes, info)) {
        if (info.header_end > bytes.size()) __builtin_trap();
        if (info.doc_value_position > info.header_end) __builtin_trap();
        if (info.doc_value_size > info.header_end - info.doc_value_position) __builtin_trap();
    }
    return 0;
}
