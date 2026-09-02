#include "mkvcodec/mkvcodec.hpp"
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void hr(HRESULT result) { require(SUCCEEDED(result), "D3D11 operation failed"); }
void release_owner(void* owner) { ++*static_cast<unsigned*>(owner); }
mkvc_gpu_external_frame_config config_for(
    ID3D11Texture2D* texture, ID3D11Fence* fence, uint64_t value, unsigned& releases) {
    mkvc_gpu_external_frame_config config{};
    config.struct_size = sizeof(config); config.struct_version = 1;
    auto& frame = config.frame;
    frame.struct_size = sizeof(frame); frame.struct_version = 1;
    frame.backend = MKVC_BACKEND_INTEL;
    frame.memory_type = MKVC_GPU_MEMORY_D3D11_TEXTURE;
    frame.device_id = 1; frame.generation = value;
    frame.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    frame.width = 64; frame.height = 48; frame.plane_count = 2;
    auto& native = config.native_handle;
    native.struct_size = sizeof(native); native.struct_version = 1;
    native.type = MKVC_GPU_NATIVE_D3D11_TEXTURE; native.borrowed = 1;
    native.device_id = frame.device_id; native.generation = frame.generation;
    native.handles[0] = reinterpret_cast<uintptr_t>(texture);
    native.handles[2] = reinterpret_cast<uintptr_t>(fence);
    native.handles[3] = value;
    config.release = release_owner; config.user_data = &releases;
    return config;
}
void rejected(mkvc_gpu_external_frame_config config) {
    mkvc_gpu_frame* frame = nullptr;
    require(mkvc_gpu_frame_import_d3d11_fence(&config, &frame) == MKVC_ERROR_INVALID_ARGUMENT,
            "invalid D3D11 import was accepted");
    require(frame == nullptr, "failed import returned a frame");
}
int unavailable() {
    std::cerr << "D3D11 hardware NV12/fence capability unavailable\n";
    return std::getenv("MKVC_REQUIRE_D3D11_FENCE") ? 1 : 77;
}
}
int main(int argc, char** argv) {
    try {
        unsigned iterations = argc > 1 ? static_cast<unsigned>(std::stoul(argv[1])) : 128;
        require(iterations > 0 && iterations <= 100000, "invalid iteration count");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return unavailable();
        ComPtr<ID3D11Device5> device5;
        ComPtr<ID3D11DeviceContext4> context4;
        if (FAILED(device.As(&device5)) || FAILED(context.As(&context4))) return unavailable();
        ComPtr<ID3D11Fence> fence;
        if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&fence)))) return unavailable();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 64; desc.Height = 48;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        std::vector<uint8_t> pixels(64 * 48 * 3 / 2, 128);
        for (unsigned y = 0; y < 48; ++y)
            for (unsigned x = 0; x < 64; ++x)
                pixels[y * 64 + x] = static_cast<uint8_t>(16 + x + y);
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels.data(); data.SysMemPitch = 64;
        ComPtr<ID3D11Texture2D> source, destination;
        if (FAILED(device->CreateTexture2D(&desc, &data, &source)) ||
            FAILED(device->CreateTexture2D(&desc, nullptr, &destination))) return unavailable();
        unsigned releases = 0;
        auto config = config_for(destination.Get(), fence.Get(), 1, releases);
        auto invalid = config;
        invalid.frame.width = 62; rejected(invalid);
        invalid = config; invalid.native_handle.handles[1] = 1; rejected(invalid);
        invalid = config; invalid.native_handle.handles[2] = 0; rejected(invalid);
        invalid = config; invalid.native_handle.handles[3] = UINT64_MAX; rejected(invalid);
        invalid = config; invalid.native_handle.handles[3] = 0; rejected(invalid);

        // Two devices on the same adapter are still different synchronization domains.
        ComPtr<ID3D11Device> other;
        hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &other, nullptr, nullptr));
        ComPtr<ID3D11Device5> other5; hr(other.As(&other5));
        ComPtr<ID3D11Fence> other_fence;
        hr(other5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&other_fence)));
        invalid = config;
        invalid.native_handle.handles[2] = reinterpret_cast<uintptr_t>(other_fence.Get());
        rejected(invalid);
        require(releases == 0, "failed imports transferred owner");

        // Deliberately unsignalled fixture tests bounded timeout and later recovery.
        mkvc_gpu_frame* pending = nullptr;
        mkvcodec::check(mkvc_gpu_frame_import_d3d11_fence(&config, &pending));
        const auto timed = mkvc_gpu_frame_wait(pending, 2);
        uint32_t status = 99;
        const auto queried = mkvc_gpu_frame_query_completion(pending, &status);
        context->CopyResource(destination.Get(), source.Get());
        hr(context4->Signal(fence.Get(), 1)); context->Flush();
        const auto waited = mkvc_gpu_frame_wait(pending, 5000);
        mkvc_gpu_frame_release(pending);
        require(timed == MKVC_ERROR_TIMEOUT && queried == MKVC_OK &&
                status == MKVC_GPU_COMPLETION_PENDING && waited == MKVC_OK,
                "pending/timeout/recovery contract failed");
        require(releases == 1, "owner not released exactly once");

        // GPU copy is an external test producer, not an operation of this library.
        for (unsigned index = 0; index < iterations; ++index) {
            const uint64_t target = static_cast<uint64_t>(index) + 2;
            context->CopyResource(destination.Get(), source.Get());
            hr(context4->Signal(fence.Get(), target)); context->Flush();
            config = config_for(destination.Get(), fence.Get(), target, releases);
            auto frame = mkvcodec::GpuFrame::import_d3d11_fence(config);
            frame.wait(5000);
            mkvc_gpu_native_handle_desc exported{};
            exported.struct_size = sizeof(exported); exported.struct_version = 1;
            mkvcodec::check(mkvc_gpu_frame_get_native_handle(frame.native_handle(), &exported));
            require(exported.handles[0] == config.native_handle.handles[0] &&
                    exported.handles[2] == config.native_handle.handles[2],
                    "import changed resource identity");
            require(releases == index + 1, "owner released before frame");
        }
        require(releases == iterations + 1, "repeated imports leaked owner callbacks");

        // Test-only readback oracle, outside the import/encode API boundary.
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        hr(device->CreateTexture2D(&desc, nullptr, &readback));
        context->CopyResource(readback.Get(), destination.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        bool equal = true;
        for (unsigned y = 0; y < 72; ++y)
            for (unsigned x = 0; x < 64; ++x)
                equal &= static_cast<uint8_t*>(mapped.pData)[y * mapped.RowPitch + x] == pixels[y * 64 + x];
        context->Unmap(readback.Get(), 0);
        require(equal, "external GPU copy pixels differ");

        config = config_for(destination.Get(), fence.Get(), iterations + 1, releases);
        auto retained = mkvcodec::GpuFrame::import_d3d11_fence(config);
        destination.Reset(); fence.Reset(); // Library retains COM references.
        retained.wait(5000);
        retained.reset();
        require(releases == iterations + 2, "caller-first COM release failed");
        std::cout << "D3D11 fence timeout/recovery, device rejection, GPU copy identity/pixels, "
                  << iterations << " iterations and COM/owner lifetime passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
