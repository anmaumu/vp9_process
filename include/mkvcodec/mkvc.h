#ifndef MKVC_H
#define MKVC_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(MKVC_BUILDING_LIBRARY)
#    define MKVC_API __declspec(dllexport)
#  else
#    define MKVC_API __declspec(dllimport)
#  endif
#else
#  define MKVC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MKVC_ABI_VERSION 1u

typedef enum mkvc_result {
    MKVC_OK = 0,
    MKVC_ERROR_INVALID_ARGUMENT = 1,
    MKVC_ERROR_BUFFER_TOO_SMALL = 2,
    MKVC_ERROR_NOT_SUPPORTED = 3,
    MKVC_ERROR_INTERNAL = 4
} mkvc_result;

typedef enum mkvc_backend {
    MKVC_BACKEND_CPU = 1,
    MKVC_BACKEND_NVIDIA = 2,
    MKVC_BACKEND_INTEL = 3
} mkvc_backend;

typedef enum mkvc_codec {
    MKVC_CODEC_VP9 = 1,
    MKVC_CODEC_AV1 = 2
} mkvc_codec;

typedef struct mkvc_version {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} mkvc_version;

typedef struct mkvc_backend_capability {
    uint32_t struct_size;
    uint32_t backend;
    uint32_t codec;
    uint8_t can_decode;
    uint8_t can_encode;
    uint8_t is_hardware;
    uint8_t reserved;
} mkvc_backend_capability;

MKVC_API mkvc_result mkvc_get_version(mkvc_version* out_version);

/* Two-call API: pass NULL to query the required element count. */
MKVC_API mkvc_result mkvc_get_backend_capabilities(
    mkvc_backend_capability* capabilities,
    size_t* inout_count);

MKVC_API const char* mkvc_result_string(mkvc_result result);

#ifdef __cplusplus
}
#endif

#endif

