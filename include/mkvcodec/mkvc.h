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
    MKVC_WOULD_BLOCK = 9,             /**< Nonblocking operation cannot complete yet. */
    MKVC_ERROR_TIMEOUT = 10           /**< A bounded wait expired. */
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

/** Pixel transfer path actually exercised by a pipeline. */
typedef enum mkvc_copy_path {
    MKVC_COPY_PATH_UNKNOWN = 0,  /**< No frame has completed yet. */
    MKVC_COPY_PATH_CPU = 1,      /**< Pixels crossed caller-owned CPU memory. */
    MKVC_COPY_PATH_ZERO_COPY = 2 /**< GPU surface remained device-resident. */
} mkvc_copy_path;

/** Geometry policy used by CPU/GPU frame processing backends. */
typedef enum mkvc_frame_fit {
    MKVC_FRAME_FIT_STRETCH = 0, /**< Scale directly to the requested output. */
    MKVC_FRAME_FIT_CONTAIN = 1, /**< Preserve aspect ratio and add bars. */
    MKVC_FRAME_FIT_COVER = 2    /**< Preserve aspect ratio and center-crop. */
} mkvc_frame_fit;

/** Rotation applied clockwise after crop and before resize. */
typedef enum mkvc_frame_rotation {
    MKVC_FRAME_ROTATE_0 = 0,
    MKVC_FRAME_ROTATE_90 = 90,
    MKVC_FRAME_ROTATE_180 = 180,
    MKVC_FRAME_ROTATE_270 = 270
} mkvc_frame_rotation;

/** Immutable frame-processing plan. Zero crop size selects the full input. */
typedef struct mkvc_frame_process_config {
    uint32_t struct_size;
    uint32_t struct_version; /**< Must be 1. */
    uint32_t backend;        /**< Requested mkvc_backend; currently CPU only. */
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_width;
    uint32_t crop_height;
    uint32_t output_width;   /**< Zero preserves the post-rotation width. */
    uint32_t output_height;  /**< Zero preserves the post-rotation height. */
    uint32_t fit;            /**< One mkvc_frame_fit value. */
    uint32_t rotation;       /**< One mkvc_frame_rotation value. */
    uint8_t flip_horizontal;
    uint8_t flip_vertical;
    uint8_t reserved[2];
    uint32_t background_rgba; /**< 0xRRGGBBAA; used by contain mode. */
} mkvc_frame_process_config;

/** Device-memory representation owned behind mkvc_gpu_frame. */
typedef enum mkvc_gpu_memory_type {
    MKVC_GPU_MEMORY_D3D11_TEXTURE = 1,
    MKVC_GPU_MEMORY_VA_SURFACE = 2,
    MKVC_GPU_MEMORY_CUDA_POINTER = 3,
    MKVC_GPU_MEMORY_CUDA_ARRAY = 4,
    MKVC_GPU_MEMORY_USM = 5
} mkvc_gpu_memory_type;

/** Producer completion state for a GPU frame. */
typedef enum mkvc_gpu_completion_status {
    MKVC_GPU_COMPLETION_PENDING = 0,
    MKVC_GPU_COMPLETION_COMPLETE = 1,
    MKVC_GPU_COMPLETION_FAILED = 2
} mkvc_gpu_completion_status;

/** Backend-neutral immutable GPU frame metadata. */
typedef struct mkvc_gpu_frame_desc {
    uint32_t struct_size;
    uint32_t struct_version; /**< Must be 1. */
    uint32_t backend;        /**< One mkvc_backend. */
    uint32_t memory_type;    /**< One mkvc_gpu_memory_type. */
    uint64_t device_id;      /**< Stable only for the creating process. */
    uint64_t generation;     /**< Changes whenever the pool slot is recycled. */
    uint32_t pixel_format;
    uint32_t width;
    uint32_t height;
    uint32_t plane_count;
    uint64_t plane_offsets[4];
    uint64_t pitches[4];
    int64_t pts;
    uint32_t color_primaries;
    uint32_t color_transfer;
    uint32_t color_matrix;
    uint32_t color_range;
} mkvc_gpu_frame_desc;

/** Thread-safe cumulative pipeline observations; initialize size and version. */
typedef struct mkvc_pipeline_metrics {
    uint32_t struct_size;           /**< Size of this struct. */
    uint32_t struct_version;        /**< Must be 1 for this ABI. */
    uint64_t accepted_frames;       /**< Frames accepted or produced internally. */
    uint64_t completed_frames;      /**< Frames processed or returned to the caller. */
    uint64_t rejected_frames;       /**< Nonblocking submissions rejected as full. */
    uint64_t queue_wait_ns;         /**< Host nanoseconds blocked on queue availability/data. */
    uint64_t backend_time_ns;       /**< Host nanoseconds inside codec/container backend calls. */
    uint32_t queue_capacity;        /**< Configured application queue bound. */
    uint32_t peak_queue_depth;      /**< Largest observed application queue occupancy. */
    uint32_t hardware_pending_peak; /**< Largest observed outstanding GPU operations. */
    uint32_t copy_path;             /**< One mkvc_copy_path actually exercised. */
} mkvc_pipeline_metrics;

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
/** Reference-counted lease over one backend-owned GPU frame resource. */
typedef struct mkvc_gpu_frame mkvc_gpu_frame;

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
/** Snapshot cumulative encoder metrics without resetting them. */
MKVC_API mkvc_result mkvc_encoder_get_metrics(
    const mkvc_encoder* encoder,
    mkvc_pipeline_metrics* out_metrics);
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
/** Snapshot cumulative decoder metrics without resetting them. */
MKVC_API mkvc_result mkvc_decoder_get_metrics(
    const mkvc_decoder* decoder,
    mkvc_pipeline_metrics* out_metrics);
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
/** Apply an immutable processing plan and return a new retained frame. */
MKVC_API mkvc_result mkvc_frame_process(
    const mkvc_frame* frame,
    const mkvc_frame_process_config* config,
    mkvc_frame** out_frame);

/** Retain an existing GPU frame lease. */
MKVC_API mkvc_result mkvc_gpu_frame_retain(mkvc_gpu_frame* frame);
/** Release a GPU frame lease; NULL is accepted. */
MKVC_API void mkvc_gpu_frame_release(mkvc_gpu_frame* frame);
/** Copy immutable backend-neutral metadata from a live GPU frame lease. */
MKVC_API mkvc_result mkvc_gpu_frame_get_desc(
    const mkvc_gpu_frame* frame, mkvc_gpu_frame_desc* out_desc);
/** Query producer completion without blocking. */
MKVC_API mkvc_result mkvc_gpu_frame_query_completion(
    const mkvc_gpu_frame* frame, uint32_t* out_status);
/** Wait for producer completion; UINT32_MAX means an unbounded wait. */
MKVC_API mkvc_result mkvc_gpu_frame_wait(
    const mkvc_gpu_frame* frame, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
