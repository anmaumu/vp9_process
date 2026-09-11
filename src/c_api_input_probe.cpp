/**
 * @file c_api_input_probe.cpp
 * @brief C ABI adapter for backend-neutral input video metadata probing.
 */
#include <string>
#include <utility>

#include "c_api_internal.hpp"
#include "input_video_probe.hpp"

extern "C" mkvc_result mkvc_probe_input(const char* input_path_utf8, mkvc_video_info* out_info) {
    mkvc_last_error.clear();
    if (input_path_utf8 == nullptr || input_path_utf8[0] == '\0' || out_info == nullptr ||
        out_info->struct_size < sizeof(*out_info) || out_info->struct_version != 1) {
        return mkvc::capi::fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid input probe arguments");
    }
    return mkvc::capi::guard("unknown input probe failure", [&] {
        mkvc_video_info value{};
        std::string error;
        const mkvc_result result = mkvc::probe_input_video(input_path_utf8, value, error);
        if (result != MKVC_OK) return mkvc::capi::fail(result, std::move(error));
        *out_info = value;
        return MKVC_OK;
    });
}
