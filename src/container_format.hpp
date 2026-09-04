#pragma once

#include <string>

namespace mkvc {

enum class ContainerFormat { WebM, Matroska };

/** Resolve the required container from a case-insensitive .webm/.mkv suffix. */
bool resolve_container_format(const char* path, ContainerFormat& format,
                              std::string& error);

/** Validate that an existing input's EBML DocType agrees with its extension. */
bool validate_container_doc_type(const char* path, ContainerFormat format,
                                 std::string& error);

/** Convert libwebm's WebM EBML header to Matroska after a successful finalize. */
bool finalize_container_doc_type(const char* path, ContainerFormat format,
                                 std::string& error);

}  // namespace mkvc
