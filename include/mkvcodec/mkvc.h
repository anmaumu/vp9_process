#ifndef MKVC_H
#define MKVC_H

/**
 * @file mkvc.h
 * @brief Stable C ABI for MKVCodec encode, decode, frame, and capability APIs.
 *
 * All functions are exception-safe across the ABI boundary. Functions returning
 * mkvc_result publish additional diagnostic text through mkvc_get_last_error().
 */

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

/** Current binary interface version. */
#define MKVC_ABI_VERSION 1u

/** Stable result and stream-status values returned by the C ABI. */
typedef enum mkvc_result {
    MKVC_OK = 0,                       /**< Operation completed successfully. */
    MKVC_ERROR_INVALID_ARGUMENT = 1,   /**< An argument or public struct is invalid. */
    MKVC_ERROR_BUFFER_TOO_SMALL = 2,  /**< Caller-provided output capacity is insufficient. */
    MKVC_ERROR_NOT_SUPPORTED = 3,     /**< Requested codec, format, or backend is unsupported. */
    MKVC_ERROR_INTERNAL = 4,          /**< Unexpected implementation failure. */
    MKVC_ERROR_INVALID_STATE = 5,     /**< Operation is invalid in the handle's current state. */
    MKVC_ERROR_IO = 6,                /**< Container or filesystem I/O failed. */
    MKVC_ERROR_CODEC = 7,             /**< Codec initialization or processing failed. */
    MKVC_END_OF_STREAM = 8,           /**< Decoder reached a clean end of stream. */
    MKVC_WOULD_BLOCK = 9              /**< Nonblocking submission found a full queue. */
} mkvc_result;

/** Backend families addressable through the common API. */
typedef enum mkvc_backend {
    MKVC_BACKEND_CPU = 1,     /**< Software codec backend. */
    MKVC_BACKEND_NVIDIA = 2,  /**< NVIDIA NVDEC/NVENC backend. */
    MKVC_BACKEND_INTEL = 3    /**< Intel oneVPL backend. */
} mkvc_backend;

/** Video codecs allowed by the product policy. */
typedef enum mkvc_codec {
    MKVC_CODEC_VP9 = 1, /**< VP9. */
    MKVC_CODEC_AV1 = 2  /**< AV1. */
} mkvc_codec;

/** Library and ABI version output. Initialize struct_size before use. */
typedef struct mkvc_version {
    uint32_t struct_size;  /**< Size of this struct supplied by the caller. */
    uint32_t abi_version; /**< MKVC_ABI_VERSION used by the loaded library. */
    uint32_t major;       /**< Product major version. */
    uint32_t minor;       /**< Product minor version. */
    uint32_t patch;       /**< Product patch version. */
} mkvc_version;

/** A codec capability exposed by one initialized backend. */
typedef struct mkvc_backend_capability {
    uint32_t struct_size; /**< Size of this struct. */
    uint32_t backend;    /**< One mkvc_backend value. */
    uint32_t codec;      /**< One mkvc_codec value. */
    uint8_t can_decode;  /**< Nonzero when decode is available. */
    uint8_t can_encode;  /**< Nonzero when encode is available. */
    uint8_t is_hardware; /**< Nonzero for a hardware-accelerated backend. */
    uint8_t reserved;    /**< Reserved; must be ignored. */
} mkvc_backend_capability;

/** 8-bit CPU pixel formats accepted or returned by the current ABI. */
typedef enum mkvc_pixel_format {
    MKVC_PIXEL_FORMAT_I420 = 1,   /**< Planar Y, U, V 4:2:0. */
    MKVC_PIXEL_FORMAT_NV12 = 2,   /**< Y plus interleaved UV 4:2:0. */
    MKVC_PIXEL_FORMAT_BGR24 = 3,  /**< Interleaved B, G, R bytes. */
    MKVC_PIXEL_FORMAT_RGB24 = 4,  /**< Interleaved R, G, B bytes. */
    MKVC_PIXEL_FORMAT_BGRA32 = 5  /**< Interleaved B, G, R, A bytes. */
} mkvc_pixel_format;

/** Encoder creation parameters, including optional bounded asynchronous submission. */
typedef struct mkvc_encoder_config {
    uint32_t struct_size;              /**< Size of this struct. */
    uint32_t struct_version;           /**< Must be 1 for this ABI. */
    const char* output_path_utf8;      /**< Null-terminated output path. */
    uint32_t codec;                    /**< Requested mkvc_codec. */
    uint32_t backend;                  /**< Requested mkvc_backend. */
    uint32_t width;                    /**< Even coded width in pixels. */
    uint32_t height;                   /**< Even coded height in pixels. */
    uint32_t fps_num;                  /**< Frame-rate numerator. */
    uint32_t fps_den;                  /**< Frame-rate denominator. */
    uint32_t quality;                  /**< Quality from 0 (best) to 63 (worst). */
    uint32_t keyframe_interval_frames; /**< Zero selects the four-second default. */
    uint32_t threads;                  /**< Zero selects automatic thread count. */
    uint32_t queue_size;               /**< Zero is synchronous; positive values enable a bounded worker queue. */
} mkvc_encoder_config;

/**
 * @brief Borrowed CPU frame view.
 *
 * Encoder input memory is copied before mkvc_encoder_write_frame() returns.
 * Decoder output pointers remain valid while the owning mkvc_frame is retained.
 */
typedef struct mkvc_frame_view {
    uint32_t struct_size;       /**< Size of this struct. */
    uint32_t struct_version;    /**< Must be 1 for this ABI. */
    uint32_t pixel_format;      /**< One mkvc_pixel_format value. */
    uint32_t width;             /**< Visible width in pixels. */
    uint32_t height;            /**< Visible height in pixels. */
    const uint8_t* planes[4];   /**< Format-dependent plane pointers. */
    int32_t strides[4];         /**< Plane row strides in bytes. */
    int64_t pts;                /**< Input timebase units or decoded nanoseconds. */
} mkvc_frame_view;

/**
 * @brief Caller-owned writable CPU frame view used for format conversion.
 *
 * The caller allocates every required plane. width and height must match the
 * decoded source frame. Pointers are never retained after mkvc_frame_copy_to().
 */
typedef struct mkvc_mutable_frame_view {
    uint32_t struct_size;     /**< Size of this struct. */
    uint32_t struct_version;  /**< Must be 1 for this ABI. */
    uint32_t pixel_format;    /**< Requested mkvc_pixel_format. */
    uint32_t width;           /**< Destination width in pixels. */
    uint32_t height;          /**< Destination height in pixels. */
    uint8_t* planes[4];       /**< Caller-owned destination planes. */
    int32_t strides[4];       /**< Destination row strides in bytes. */
    int64_t pts;              /**< Receives decoded PTS in nanoseconds. */
} mkvc_mutable_frame_view;

/** Opaque encoder handle. */
typedef struct mkvc_encoder mkvc_encoder;
/** Opaque decoder handle. */
typedef struct mkvc_decoder mkvc_decoder;
/** Reference-counted decoded frame handle. */
typedef struct mkvc_frame mkvc_frame;

/** Synchronous decoder creation parameters. */
typedef struct mkvc_decoder_config {
    uint32_t struct_size;         /**< Size of this struct. */
    uint32_t struct_version;      /**< Must be 1 for this ABI. */
    const char* input_path_utf8;  /**< Null-terminated input path. */
    uint32_t codec;               /**< Requested mkvc_codec. */
    uint32_t backend;             /**< Requested mkvc_backend. */
    uint32_t threads;             /**< Zero selects codec default. */
    uint32_t prefetch;            /**< Zero is synchronous; positive values bound the read-ahead queue. */
} mkvc_decoder_config;

/** Query the loaded library version. */
MKVC_API mkvc_result mkvc_get_version(mkvc_version* out_version);

/** Two-call capability query; pass NULL to obtain the required element count. */
MKVC_API mkvc_result mkvc_get_backend_capabilities(
    mkvc_backend_capability* capabilities,
    size_t* inout_count);

/** Return a static English name for a result code. */
MKVC_API const char* mkvc_result_string(mkvc_result result);

/** Create an encoder. queue_size selects synchronous or asynchronous operation. */
MKVC_API mkvc_result mkvc_encoder_create(
    const mkvc_encoder_config* config,
    mkvc_encoder** out_encoder);
/** Copy and submit one CPU frame to an encoder. */
MKVC_API mkvc_result mkvc_encoder_write_frame(
    mkvc_encoder* encoder,
    const mkvc_frame_view* frame);
/** Submit one frame without waiting for queue space; may return MKVC_WOULD_BLOCK. */
MKVC_API mkvc_result mkvc_encoder_try_write_frame(
    mkvc_encoder* encoder,
    const mkvc_frame_view* frame);
/** Drain currently submitted encoder work without closing the handle. */
MKVC_API mkvc_result mkvc_encoder_flush(mkvc_encoder* encoder);
/** Drain, finalize the container, and close the encoder idempotently. */
MKVC_API mkvc_result mkvc_encoder_close(mkvc_encoder* encoder);
/** Destroy an encoder handle; NULL is accepted. */
MKVC_API void mkvc_encoder_destroy(mkvc_encoder* encoder);

/** Return thread-local error detail valid until the next API call on this thread. */
MKVC_API const char* mkvc_get_last_error(void);

/** Create a synchronous decoder. The caller owns the returned handle. */
MKVC_API mkvc_result mkvc_decoder_create(
    const mkvc_decoder_config* config,
    mkvc_decoder** out_decoder);
/** Read one decoded frame or return MKVC_END_OF_STREAM. */
MKVC_API mkvc_result mkvc_decoder_read(
    mkvc_decoder* decoder,
    mkvc_frame** out_frame);
/** Close the decoder and release codec/container resources idempotently. */
MKVC_API mkvc_result mkvc_decoder_close(mkvc_decoder* decoder);
/** Destroy a decoder handle; NULL is accepted. */
MKVC_API void mkvc_decoder_destroy(mkvc_decoder* decoder);

/** Increment a decoded frame's reference count. */
MKVC_API void mkvc_frame_retain(mkvc_frame* frame);
/** Decrement a decoded frame's reference count and destroy it at zero. */
MKVC_API void mkvc_frame_release(mkvc_frame* frame);
/** Populate a borrowed view whose pointers are owned by frame. */
MKVC_API mkvc_result mkvc_frame_get_view(
    const mkvc_frame* frame,
    mkvc_frame_view* out_view);
/** Copy or convert a decoded I420 frame into caller-owned CPU memory. */
MKVC_API mkvc_result mkvc_frame_copy_to(
    const mkvc_frame* frame,
    mkvc_mutable_frame_view* destination);

#ifdef __cplusplus
}
#endif

#endif
