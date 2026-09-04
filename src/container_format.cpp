#include "container_format.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mkvc {
namespace {

constexpr uint64_t kEbmlHeader = 0x1A45DFA3;
constexpr uint64_t kDocType = 0x4282;

bool read_vint(const std::vector<uint8_t>& data, size_t& position,
               bool keep_marker, uint64_t& value, size_t& width) {
    if (position >= data.size() || data[position] == 0) return false;
    uint8_t mask = 0x80;
    width = 1;
    while ((data[position] & mask) == 0) {
        mask >>= 1;
        if (++width > 8) return false;
    }
    if (position + width > data.size()) return false;
    value = keep_marker ? data[position] : static_cast<uint8_t>(data[position] & ~mask);
    for (size_t index = 1; index < width; ++index)
        value = (value << 8) | data[position + index];
    position += width;
    return true;
}

bool write_size(std::vector<uint8_t>& data, size_t position, size_t width,
                uint64_t value) {
    const uint64_t limit = width == 8 ? std::numeric_limits<uint64_t>::max()
                                      : ((uint64_t{1} << (7 * width)) - 1);
    if (value >= limit) return false;
    for (size_t index = 0; index < width; ++index) {
        data[position + width - 1 - index] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
    data[position] |= static_cast<uint8_t>(1u << (8 - width));
    return true;
}

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

bool parse_header(const std::vector<uint8_t>& bytes, HeaderInfo& info) {
    size_t position = 0;
    uint64_t id = 0;
    size_t width = 0;
    if (!read_vint(bytes, position, true, id, width) || id != kEbmlHeader) return false;
    info.payload_size_position = position;
    if (!read_vint(bytes, position, false, info.payload_size,
                   info.payload_size_width)) return false;
    if (info.payload_size > bytes.size() - position) return false;
    info.header_end = position + static_cast<size_t>(info.payload_size);
    while (position < info.header_end) {
        uint64_t element_id = 0, element_size = 0;
        if (!read_vint(bytes, position, true, element_id, width)) return false;
        const size_t size_position = position;
        size_t size_width = 0;
        if (!read_vint(bytes, position, false, element_size, size_width) ||
            element_size > info.header_end - position) return false;
        if (element_id == kDocType) {
            info.doc_size_position = size_position;
            info.doc_size_width = size_width;
            info.doc_value_position = position;
            info.doc_value_size = static_cast<size_t>(element_size);
            info.doc_type.assign(reinterpret_cast<const char*>(bytes.data() + position),
                                 info.doc_value_size);
            return true;
        }
        position += static_cast<size_t>(element_size);
    }
    return false;
}

bool read_header(const char* path, std::vector<uint8_t>& bytes, HeaderInfo& info,
                 std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "failed to open container header"; return false; }
    bytes.resize(4096);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    bytes.resize(static_cast<size_t>(input.gcount()));
    if (!parse_header(bytes, info)) {
        error = "invalid or unsupported EBML header";
        return false;
    }
    return true;
}

bool replace_file(const std::filesystem::path& temporary,
                  const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

}  // namespace

bool resolve_container_format(const char* path, ContainerFormat& format,
                              std::string& error) {
    if (path == nullptr || *path == '\0') { error = "container path is empty"; return false; }
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".webm") { format = ContainerFormat::WebM; return true; }
    if (extension == ".mkv") { format = ContainerFormat::Matroska; return true; }
    error = "container extension must be .webm or .mkv";
    return false;
}

bool validate_container_doc_type(const char* path, ContainerFormat format,
                                 std::string& error) {
    std::vector<uint8_t> bytes;
    HeaderInfo info;
    if (!read_header(path, bytes, info, error)) return false;
    const std::string_view expected = format == ContainerFormat::WebM ? "webm" : "matroska";
    if (info.doc_type != expected) {
        error = "container extension and EBML DocType disagree";
        return false;
    }
    return true;
}

bool finalize_container_doc_type(const char* path, ContainerFormat format,
                                 std::string& error) {
    if (format == ContainerFormat::WebM) return validate_container_doc_type(path, format, error);
    std::vector<uint8_t> bytes;
    HeaderInfo info;
    if (!read_header(path, bytes, info, error)) return false;
    if (info.doc_type != "webm") { error = "libwebm output did not contain WebM DocType"; return false; }
    constexpr std::string_view replacement = "matroska";
    const size_t delta = replacement.size() - info.doc_value_size;
    std::vector<uint8_t> header(bytes.begin(), bytes.begin() + info.header_end);
    header.erase(header.begin() + info.doc_value_position,
                 header.begin() + info.doc_value_position + info.doc_value_size);
    header.insert(header.begin() + info.doc_value_position,
                  replacement.begin(), replacement.end());
    if (!write_size(header, info.doc_size_position, info.doc_size_width,
                    replacement.size()) ||
        !write_size(header, info.payload_size_position, info.payload_size_width,
                    info.payload_size + delta)) {
        error = "Matroska DocType does not fit the existing EBML size fields";
        return false;
    }
    const std::filesystem::path destination(path);
    const auto temporary = destination.string() + ".mkvc-container.tmp";
    std::ifstream input(destination, std::ios::binary);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!input || !output) { error = "failed to create Matroska replacement"; return false; }
    output.write(reinterpret_cast<const char*>(header.data()), header.size());
    input.seekg(static_cast<std::streamoff>(info.header_end));
    // Encoder finalization can run on a worker with a deliberately small
    // stack. Keep the streaming buffer on the heap so container conversion
    // cannot exhaust that stack even when this Matroska branch is optimized
    // into the same function frame as the WebM fast path.
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), buffer.size());
        output.write(buffer.data(), input.gcount());
    }
    output.flush();
    input.close(); output.close();
    if (!output || !replace_file(temporary, destination)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = "failed to atomically install Matroska output";
        return false;
    }
    return validate_container_doc_type(path, format, error);
}

}  // namespace mkvc
