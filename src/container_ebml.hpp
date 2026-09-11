#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mkvc::container_detail {

/** Parsed locations and values from the bounded EBML header prefix. */
struct HeaderInfo {
    size_t payload_size_position = 0;
    size_t payload_size_width = 0;
    uint64_t payload_size = 0;
    size_t header_end = 0;
    size_t doc_size_position = 0;
    size_t doc_size_width = 0;
    size_t doc_value_position = 0;
    size_t doc_value_size = 0;
    std::string doc_type;
};

/** Parse one bounded in-memory EBML header prefix without file-system access. */
bool parse_header_bytes(const std::vector<uint8_t>& bytes, HeaderInfo& info);

/** Read and parse the bounded EBML header prefix from a container. */
bool read_header(const char* path, std::vector<uint8_t>& bytes, HeaderInfo& info,
                 std::string& error);

/** Rewrite a fixed-width EBML variable-length size field in place. */
bool write_size(std::vector<uint8_t>& data, size_t position, size_t width, uint64_t value);

/** Atomically replace a destination with a fully written temporary file. */
bool replace_file(const std::filesystem::path& temporary, const std::filesystem::path& destination);

}  // namespace mkvc::container_detail
