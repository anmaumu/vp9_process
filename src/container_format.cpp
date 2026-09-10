#include "container_format.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "container_ebml.hpp"

namespace mkvc {

bool resolve_container_format(const char* path, ContainerFormat& format, std::string& error) {
    if (path == nullptr || *path == '\0') {
        error = "container path is empty";
        return false;
    }
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".webm") {
        format = ContainerFormat::WebM;
        return true;
    }
    if (extension == ".mkv") {
        format = ContainerFormat::Matroska;
        return true;
    }
    error = "container extension must be .webm or .mkv";
    return false;
}

bool validate_container_doc_type(const char* path, ContainerFormat format, std::string& error) {
    std::vector<uint8_t> bytes;
    container_detail::HeaderInfo info;
    if (!container_detail::read_header(path, bytes, info, error)) return false;
    const std::string_view expected = format == ContainerFormat::WebM ? "webm" : "matroska";
    if (info.doc_type != expected) {
        error = "container extension and EBML DocType disagree";
        return false;
    }
    return true;
}

bool finalize_container_doc_type(const char* path, ContainerFormat format, std::string& error) {
    if (format == ContainerFormat::WebM) return validate_container_doc_type(path, format, error);
    std::vector<uint8_t> bytes;
    container_detail::HeaderInfo info;
    if (!container_detail::read_header(path, bytes, info, error)) return false;
    if (info.doc_type != "webm") {
        error = "libwebm output did not contain WebM DocType";
        return false;
    }
    constexpr std::string_view replacement = "matroska";
    const size_t delta = replacement.size() - info.doc_value_size;
    std::vector<uint8_t> header(bytes.begin(), bytes.begin() + info.header_end);
    header.erase(header.begin() + info.doc_value_position,
                 header.begin() + info.doc_value_position + info.doc_value_size);
    header.insert(header.begin() + info.doc_value_position, replacement.begin(), replacement.end());
    if (!container_detail::write_size(header, info.doc_size_position, info.doc_size_width,
                                      replacement.size()) ||
        !container_detail::write_size(header, info.payload_size_position, info.payload_size_width,
                                      info.payload_size + delta)) {
        error = "Matroska DocType does not fit the existing EBML size fields";
        return false;
    }
    const std::filesystem::path destination(path);
    const auto temporary = destination.string() + ".mkvc-container.tmp";
    std::ifstream input(destination, std::ios::binary);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        error = "failed to create Matroska replacement";
        return false;
    }
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
    input.close();
    output.close();
    if (!output || !container_detail::replace_file(temporary, destination)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = "failed to atomically install Matroska output";
        return false;
    }
    return validate_container_doc_type(path, format, error);
}

}  // namespace mkvc
