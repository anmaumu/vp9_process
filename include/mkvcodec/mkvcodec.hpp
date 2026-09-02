#pragma once

/**
 * @file mkvcodec.hpp
 * @brief Header-only C++17 RAII facade over the stable MKVCodec C ABI.
 */

#include "mkvcodec/mkvc.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace mkvcodec {

/** C ABI failure copied into a normal C++ exception at the wrapper boundary. */
class ResultError : public std::runtime_error {
 public:
    ResultError(mkvc_result result, std::string message)
        : std::runtime_error(std::move(message)), result_(result) {}
    mkvc_result result() const noexcept { return result_; }

 private:
    mkvc_result result_;
};

/** Throw ResultError for a non-success C ABI result. */
inline void check(mkvc_result result) {
    if (result == MKVC_OK) return;
    const char* detail = mkvc_get_last_error();
    throw ResultError(result,
        detail != nullptr && detail[0] != '\0' ? detail
                                                : mkvc_result_string(result));
}

/** Move-only completion lease for an asynchronous CPU submission. */
class Submission {
 public:
    Submission() noexcept = default;
    explicit Submission(mkvc_submission* handle) noexcept : handle_(handle) {}
    ~Submission() { reset(); }
    Submission(const Submission&) = delete;
    Submission& operator=(const Submission&) = delete;
    Submission(Submission&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    Submission& operator=(Submission&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const noexcept { return handle_ != nullptr; }
    uint32_t status() const {
        ensure_open();
        uint32_t value = MKVC_SUBMISSION_PENDING;
        check(mkvc_submission_query(handle_, &value));
        return value;
    }
    void wait(uint32_t timeout_ms = UINT32_MAX) const {
        ensure_open();
        check(mkvc_submission_wait(handle_, timeout_ms));
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            mkvc_submission_release(handle_);
            handle_ = nullptr;
        }
    }
    mkvc_submission* native_handle() const noexcept { return handle_; }

 private:
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec submission is closed");
        }
    }
    mkvc_submission* handle_ = nullptr;
};

/** Move-only writable lease over one native CPU frame-pool slot. */
class CpuBuffer {
 public:
    CpuBuffer() noexcept = default;
    ~CpuBuffer() { reset(); }
    CpuBuffer(const CpuBuffer&) = delete;
    CpuBuffer& operator=(const CpuBuffer&) = delete;
    CpuBuffer(CpuBuffer&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    CpuBuffer& operator=(CpuBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const noexcept { return handle_ != nullptr; }
    mkvc_cpu_buffer_desc descriptor() const {
        ensure_open();
        mkvc_cpu_buffer_desc value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_cpu_buffer_get_desc(handle_, &value));
        return value;
    }
    mkvc_mutable_frame_view view() const {
        ensure_open();
        mkvc_mutable_frame_view value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_cpu_buffer_get_view(handle_, &value));
        return value;
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            mkvc_cpu_buffer_release(handle_);
            handle_ = nullptr;
        }
    }
    mkvc_cpu_buffer* native_handle() const noexcept { return handle_; }

 private:
    friend class CpuFramePool;
    explicit CpuBuffer(mkvc_cpu_buffer* handle) noexcept : handle_(handle) {}
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec CPU buffer is closed");
        }
    }
    mkvc_cpu_buffer* handle_ = nullptr;
};

/** Fixed-capacity native CPU frame pool with generation-safe buffer leases. */
class CpuFramePool {
 public:
    explicit CpuFramePool(const mkvc_cpu_frame_pool_config& config) {
        check(mkvc_cpu_frame_pool_create(&config, &handle_));
    }
    CpuFramePool(uint32_t pixel_format, uint32_t width, uint32_t height,
                 uint32_t capacity) {
        mkvc_cpu_frame_pool_config config{};
        config.struct_size = sizeof(config);
        config.struct_version = 1;
        config.pixel_format = pixel_format;
        config.width = width;
        config.height = height;
        config.capacity = capacity;
        check(mkvc_cpu_frame_pool_create(&config, &handle_));
    }
    ~CpuFramePool() { reset(); }
    CpuFramePool(const CpuFramePool&) = delete;
    CpuFramePool& operator=(const CpuFramePool&) = delete;
    CpuFramePool(CpuFramePool&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    CpuFramePool& operator=(CpuFramePool&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    CpuBuffer acquire(uint32_t timeout_ms = UINT32_MAX) {
        ensure_open();
        mkvc_cpu_buffer* buffer = nullptr;
        check(mkvc_cpu_frame_pool_acquire(handle_, timeout_ms, &buffer));
        return CpuBuffer(buffer);
    }
    std::optional<CpuBuffer> try_acquire() {
        ensure_open();
        mkvc_cpu_buffer* buffer = nullptr;
        const mkvc_result result =
            mkvc_cpu_frame_pool_acquire(handle_, 0, &buffer);
        if (result == MKVC_WOULD_BLOCK) return std::nullopt;
        check(result);
        return CpuBuffer(buffer);
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            mkvc_cpu_frame_pool_destroy(handle_);
            handle_ = nullptr;
        }
    }
    mkvc_cpu_frame_pool* native_handle() const noexcept { return handle_; }

 private:
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec CPU frame pool is closed");
        }
    }
    mkvc_cpu_frame_pool* handle_ = nullptr;
};

/** Move-only retained CPU decoder output. Borrowed views live with this owner. */
class Frame {
 public:
    Frame() noexcept = default;
    ~Frame() { reset(); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    Frame& operator=(Frame&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    mkvc_frame_view view() const {
        ensure_open();
        mkvc_frame_view value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_frame_get_view(handle_, &value));
        return value;
    }
    Frame process(const mkvc_frame_process_config& config) const {
        ensure_open();
        mkvc_frame* output = nullptr;
        check(mkvc_frame_process(handle_, &config, &output));
        return Frame(output);
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            mkvc_frame_release(handle_);
            handle_ = nullptr;
        }
    }
    mkvc_frame* native_handle() const noexcept { return handle_; }

 private:
    friend class Decoder;
    explicit Frame(mkvc_frame* handle) noexcept : handle_(handle) {}
    void ensure_open() const {
        if (handle_ == nullptr) throw std::logic_error("MKVCodec frame is closed");
    }
    mkvc_frame* handle_ = nullptr;
};

/** Move-only GPU frame lease suitable for native-handle interop. */
class GpuFrame {
 public:
    GpuFrame() noexcept = default;
    static GpuFrame import_external(
        const mkvc_gpu_external_frame_config& config) {
        mkvc_gpu_frame* frame = nullptr;
        check(mkvc_gpu_frame_import_external(&config, &frame));
        return GpuFrame(frame);
    }

    static GpuFrame import_cuda_event(
        const mkvc_gpu_external_frame_config& config) {
        mkvc_gpu_frame* frame = nullptr;
        check(mkvc_gpu_frame_import_cuda_event(&config, &frame));
        return GpuFrame(frame);
    }
    /** Import an Intel VA surface with native per-surface completion polling. */
    static GpuFrame import_va_surface(
        const mkvc_gpu_external_frame_config& config) {
        mkvc_gpu_frame* frame = nullptr;
        check(mkvc_gpu_frame_import_va_surface(&config, &frame));
        return GpuFrame(frame);
    }
    ~GpuFrame() { reset(); }
    GpuFrame(const GpuFrame&) = delete;
    GpuFrame& operator=(const GpuFrame&) = delete;
    GpuFrame(GpuFrame&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    GpuFrame& operator=(GpuFrame&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    mkvc_gpu_frame_desc descriptor() const {
        ensure_open();
        mkvc_gpu_frame_desc value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_gpu_frame_get_desc(handle_, &value));
        return value;
    }
    mkvc_gpu_native_handle_desc native_resource() const {
        ensure_open();
        mkvc_gpu_native_handle_desc value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_gpu_frame_get_native_handle(handle_, &value));
        return value;
    }
    uint32_t completion_status() const {
        ensure_open();
        uint32_t value = MKVC_GPU_COMPLETION_PENDING;
        check(mkvc_gpu_frame_query_completion(handle_, &value));
        return value;
    }
    void wait(uint32_t timeout_ms = UINT32_MAX) const {
        ensure_open();
        check(mkvc_gpu_frame_wait(handle_, timeout_ms));
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            mkvc_gpu_frame_release(handle_);
            handle_ = nullptr;
        }
    }
    mkvc_gpu_frame* native_handle() const noexcept { return handle_; }

 private:
    friend class Decoder;
    explicit GpuFrame(mkvc_gpu_frame* handle) noexcept : handle_(handle) {}
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec GPU frame is closed");
        }
    }
    mkvc_gpu_frame* handle_ = nullptr;
};

/** Move-only decoder facade returning retained CPU or GPU frame leases. */
class Decoder {
 public:
    explicit Decoder(const mkvc_decoder_config& config) {
        check(mkvc_decoder_create(&config, &handle_));
    }
    ~Decoder() { reset(); }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          closed_(std::exchange(other.closed_, true)) {}
    Decoder& operator=(Decoder&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
            closed_ = std::exchange(other.closed_, true);
        }
        return *this;
    }
    void set_copy_policy(const mkvc_copy_policy& policy) {
        ensure_open();
        check(mkvc_decoder_set_copy_policy(handle_, &policy));
    }
    std::optional<Frame> read() {
        ensure_open();
        mkvc_frame* frame = nullptr;
        const mkvc_result result = mkvc_decoder_read(handle_, &frame);
        if (result == MKVC_END_OF_STREAM) return std::nullopt;
        check(result);
        return Frame(frame);
    }
    std::optional<GpuFrame> read_gpu() {
        ensure_open();
        mkvc_gpu_frame* frame = nullptr;
        const mkvc_result result = mkvc_decoder_read_gpu(handle_, &frame);
        if (result == MKVC_END_OF_STREAM) return std::nullopt;
        check(result);
        return GpuFrame(frame);
    }
    mkvc_pipeline_metrics metrics() const {
        ensure_open();
        mkvc_pipeline_metrics value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_decoder_get_metrics(handle_, &value));
        return value;
    }
    void close() {
        ensure_open();
        if (closed_) return;
        const mkvc_result result = mkvc_decoder_close(handle_);
        closed_ = true;
        check(result);
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            if (!closed_) (void)mkvc_decoder_close(handle_);
            mkvc_decoder_destroy(handle_);
            handle_ = nullptr;
            closed_ = true;
        }
    }
    mkvc_decoder* native_handle() const noexcept { return handle_; }

 private:
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec decoder is closed");
        }
    }
    mkvc_decoder* handle_ = nullptr;
    bool closed_ = false;
};

/** Move-only encoder facade retaining the stable C handle underneath. */
class Encoder {
 public:
    explicit Encoder(const mkvc_encoder_config& config) {
        check(mkvc_encoder_create(&config, &handle_));
    }
    ~Encoder() { reset(); }
    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          closed_(std::exchange(other.closed_, true)) {}
    Encoder& operator=(Encoder&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
            closed_ = std::exchange(other.closed_, true);
        }
        return *this;
    }

    void write(const mkvc_frame_view& frame) {
        ensure_open();
        check(mkvc_encoder_write_frame(handle_, &frame));
    }
    void set_copy_policy(const mkvc_copy_policy& policy) {
        ensure_open();
        check(mkvc_encoder_set_copy_policy(handle_, &policy));
    }
    void write(const GpuFrame& frame) {
        ensure_open();
        if (!frame) throw std::logic_error("MKVCodec GPU frame is closed");
        check(mkvc_encoder_write_gpu_frame(handle_, frame.native_handle()));
    }
    bool try_write(const mkvc_frame_view& frame) {
        ensure_open();
        const mkvc_result result = mkvc_encoder_try_write_frame(handle_, &frame);
        if (result == MKVC_WOULD_BLOCK) return false;
        check(result);
        return true;
    }
    Submission submit(const CpuBuffer& buffer, int64_t pts = -1) {
        ensure_open();
        if (!buffer) throw std::logic_error("MKVCodec CPU buffer is closed");
        mkvc_submission* submission = nullptr;
        check(mkvc_encoder_submit_cpu_buffer(
            handle_, buffer.native_handle(), pts, &submission));
        return Submission(submission);
    }
    void flush() { ensure_open(); check(mkvc_encoder_flush(handle_)); }
    void cancel() { ensure_open(); check(mkvc_encoder_cancel(handle_)); }
    void close() {
        ensure_open();
        if (closed_) return;
        const mkvc_result result = mkvc_encoder_close(handle_);
        closed_ = true;
        check(result);
    }
    mkvc_pipeline_metrics metrics() const {
        ensure_open();
        mkvc_pipeline_metrics value{};
        value.struct_size = sizeof(value);
        value.struct_version = 1;
        check(mkvc_encoder_get_metrics(handle_, &value));
        return value;
    }
    void reset() noexcept {
        if (handle_ != nullptr) {
            if (!closed_) (void)mkvc_encoder_close(handle_);
            mkvc_encoder_destroy(handle_);
            handle_ = nullptr;
            closed_ = true;
        }
    }
    mkvc_encoder* native_handle() const noexcept { return handle_; }

 private:
    void ensure_open() const {
        if (handle_ == nullptr) {
            throw std::logic_error("MKVCodec encoder is closed");
        }
    }
    mkvc_encoder* handle_ = nullptr;
    bool closed_ = false;
};

}  // namespace mkvcodec
