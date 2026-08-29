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
    MKVC_ERROR_INTERNAL = 4,
    MKVC_ERROR_INVALID_STATE = 5,
    MKVC_ERROR_IO = 6,
    MKVC_ERROR_CODEC = 7,
    MKVC_END_OF_STREAM = 8
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

typedef enum mkvc_pixel_format {
    MKVC_PIXEL_FORMAT_I420 = 1,
    MKVC_PIXEL_FORMAT_NV12 = 2,
    MKVC_PIXEL_FORMAT_BGR24 = 3,
    MKVC_PIXEL_FORMAT_RGB24 = 4,
    MKVC_PIXEL_FORMAT_BGRA32 = 5
} mkvc_pixel_format;

typedef struct mkvc_encoder_config {
    uint32_t struct_size;
    uint32_t struct_version;
    const char* output_path_utf8;
    uint32_t codec;
    uint32_t backend;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t quality;
    uint32_t keyframe_interval_frames;
    uint32_t threads;
} mkvc_encoder_config;

typedef struct mkvc_frame_view {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t pixel_format;
    uint32_t width;
    uint32_t height;
    const uint8_t* planes[4];
    int32_t strides[4];
    int64_t pts;
} mkvc_frame_view;

typedef struct mkvc_encoder mkvc_encoder;
typedef struct mkvc_decoder mkvc_decoder;
typedef struct mkvc_frame mkvc_frame;

typedef struct mkvc_decoder_config {
    uint32_t struct_size;
    uint32_t struct_version;
    const char* input_path_utf8;
    uint32_t codec;
    uint32_t backend;
    uint32_t threads;
} mkvc_decoder_config;

MKVC_API mkvc_result mkvc_get_version(mkvc_version* out_version);

/* Two-call API: pass NULL to query the required element count. */
MKVC_API mkvc_result mkvc_get_backend_capabilities(
    mkvc_backend_capability* capabilities,
    size_t* inout_count);

MKVC_API const char* mkvc_result_string(mkvc_result result);

MKVC_API mkvc_result mkvc_encoder_create(
    const mkvc_encoder_config* config,
    mkvc_encoder** out_encoder);
MKVC_API mkvc_result mkvc_encoder_write_frame(
    mkvc_encoder* encoder,
    const mkvc_frame_view* frame);
MKVC_API mkvc_result mkvc_encoder_flush(mkvc_encoder* encoder);
MKVC_API mkvc_result mkvc_encoder_close(mkvc_encoder* encoder);
MKVC_API void mkvc_encoder_destroy(mkvc_encoder* encoder);

/* The returned pointer remains valid until the next API call on this thread. */
MKVC_API const char* mkvc_get_last_error(void);

MKVC_API mkvc_result mkvc_decoder_create(
    const mkvc_decoder_config* config,
    mkvc_decoder** out_decoder);
MKVC_API mkvc_result mkvc_decoder_read(
    mkvc_decoder* decoder,
    mkvc_frame** out_frame);
MKVC_API mkvc_result mkvc_decoder_close(mkvc_decoder* decoder);
MKVC_API void mkvc_decoder_destroy(mkvc_decoder* decoder);

MKVC_API void mkvc_frame_retain(mkvc_frame* frame);
MKVC_API void mkvc_frame_release(mkvc_frame* frame);
MKVC_API mkvc_result mkvc_frame_get_view(
    const mkvc_frame* frame,
    mkvc_frame_view* out_view);

#ifdef __cplusplus
}
#endif

#endif
