#include "nvidia_webm_decoder.hpp"
#include "nvidia_probe.hpp"

#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool decode_file(const char* path, bool transfer_to_worker, std::string& error) {
    mkvc_decoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.input_path_utf8 = path;
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_NVIDIA;
    auto decoder = mkvc::NvidiaWebmDecoder::create(config, error);
    if (!decoder) return false;
    const auto consume = [&decoder, &error]() {
        int frames = 0;
        int64_t previous_pts = -1;
        uint64_t luma_sum = 0;
        while (true) {
        std::unique_ptr<mkvc::DecodedFrame> frame;
        const mkvc_result result = decoder->read(frame, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result != MKVC_OK || !frame || frame->width != 160 ||
            frame->height != 128 || frame->pts_ns <= previous_pts) {
            std::cerr << "NVDEC read failure: " << error << '\n';
            return false;
        }
        previous_pts = frame->pts_ns;
        const size_t y_size = static_cast<size_t>(frame->width) * frame->height;
        for (size_t index = 0; index < y_size; ++index)
            luma_sum += frame->pixels[index];
        ++frames;
        }
        if (frames != 30 || luma_sum == 0) {
        std::cerr << "NVDEC validation failure: frames=" << frames
                  << " sum=" << luma_sum << " error=" << error << '\n';
            return false;
        }
        std::cout << "NVDEC decoded " << frames << " frames, luma sum "
                  << luma_sum << '\n';
        return true;
    };
    const bool valid = transfer_to_worker
        ? std::async(std::launch::async, consume).get() : consume();
    return valid && decoder->close(error) == MKVC_OK;
}

bool decode_gpu_file(const char* path, std::string& error) {
    mkvc_decoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.input_path_utf8 = path;
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_NVIDIA;
    auto decoder = mkvc::NvidiaWebmDecoder::create(config, error);
    if (!decoder) return false;
    int frames = 0;
    int64_t previous_pts = -1;
    mkvc_gpu_frame* retained = nullptr;
    while (true) {
        mkvc_gpu_frame* frame = nullptr;
        const mkvc_result result = decoder->read_gpu(&frame, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result != MKVC_OK || frame == nullptr) {
            error = "NVIDIA GPU read failed result=" +
                    std::to_string(static_cast<int>(result)) + " detail=" + error;
            return false;
        }
        mkvc_gpu_frame_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.struct_version = 1;
        mkvc_gpu_native_handle_desc native{};
        native.struct_size = sizeof(native);
        native.struct_version = 1;
        if (mkvc_gpu_frame_get_desc(frame, &desc) != MKVC_OK ||
            mkvc_gpu_frame_get_native_handle(frame, &native) != MKVC_OK ||
            mkvc_gpu_frame_wait(frame, 5000) != MKVC_OK ||
            desc.backend != MKVC_BACKEND_NVIDIA ||
            desc.memory_type != MKVC_GPU_MEMORY_CUDA_POINTER ||
            desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 ||
            desc.width != 160 || desc.height != 128 ||
            desc.plane_count != 2 || desc.pitches[0] < desc.width ||
            desc.plane_offsets[1] != desc.pitches[0] * desc.height ||
            desc.pts <= previous_pts ||
            native.type != MKVC_GPU_NATIVE_CUDA_POINTER ||
            native.handles[0] == 0 || native.handles[1] == 0) {
            mkvc_gpu_frame_release(frame);
            error = "invalid NVIDIA GPU frame descriptor";
            return false;
        }
        previous_pts = desc.pts;
        if (frames == 0) retained = frame;
        else mkvc_gpu_frame_release(frame);
        ++frames;
    }
    const mkvc_result close_result = decoder->close(error);
    if (frames != 30 || close_result != MKVC_OK || retained == nullptr) {
        error = "NVIDIA GPU close/count failure frames=" + std::to_string(frames) +
                " close=" + std::to_string(static_cast<int>(close_result)) +
                " detail=" + error;
        if (retained) mkvc_gpu_frame_release(retained);
        return false;
    }
    mkvc_gpu_frame_desc after_close{};
    after_close.struct_size = sizeof(after_close);
    after_close.struct_version = 1;
    const bool survived =
        mkvc_gpu_frame_get_desc(retained, &after_close) == MKVC_OK &&
        after_close.generation != 0;
    mkvc_gpu_frame_release(retained);
    return survived;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::string error;
    if (!decode_file(argv[1], false, error)) {
        std::cout << "NVDEC unavailable or failed: " << error << '\n';
        return std::getenv("MKVC_REQUIRE_NVIDIA_GPU") != nullptr ? 1 : 77;
    }
    if (!mkvc::probe_nvidia().av1_decode) {
        mkvc_decoder_config unsupported{};
        unsupported.struct_size = sizeof(unsupported);
        unsupported.struct_version = 1;
        unsupported.input_path_utf8 = argv[1];
        unsupported.codec = MKVC_CODEC_AV1;
        unsupported.backend = MKVC_BACKEND_NVIDIA;
        error.clear();
        if (mkvc::NvidiaWebmDecoder::create(unsupported, error) != nullptr ||
            error.find("unavailable") == std::string::npos) {
            std::cerr << "unsupported NVIDIA AV1 rejection failed\n";
            return 1;
        }
    }
    error.clear();
    if (!decode_file(argv[1], true, error)) {
        std::cerr << "NVDEC worker-thread transfer failed: " << error << '\n';
        return 1;
    }
    error.clear();
    if (!decode_gpu_file(argv[1], error)) {
        std::cerr << "NVDEC GPU lease validation failed: " << error << '\n';
        return 1;
    }
    return 0;
}
