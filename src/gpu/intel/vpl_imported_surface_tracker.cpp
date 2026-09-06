#include "gpu/intel/vpl_imported_surface_tracker.hpp"

#include <limits>

#include "gpu/gpu_frame.hpp"
#include "gpu/intel/intel_import_contract.hpp"
#include "gpu/intel/vpl_surface_import.hpp"

#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace mkvc::gpu::intel {
namespace {

constexpr size_t kMaxImportedInputs = 64;

}  // namespace

struct VplImportedSurfaceTracker::ImportedInput {
    mfxFrameSurface1* surface = nullptr;
    mkvc_gpu_frame* owner = nullptr;

    ~ImportedInput() {
        // VA handles have no native refcount: destroy the imported wrapper
        // before allowing the application to destroy its original surface.
        if (surface) surface->FrameInterface->Release(surface);
        mkvc_gpu_frame_release(owner);
    }
};

VplImportedSurfaceTracker::VplImportedSurfaceTracker(mfxSession session,
                                                     bool enforce_device_identity,
                                                     uint64_t device_identity) noexcept
    : session_(session),
      enforce_device_identity_(enforce_device_identity),
      device_identity_(device_identity) {}

VplImportedSurfaceTracker::~VplImportedSurfaceTracker() {
    release_wrappers_before_runtime_close();
    release_owners_after_runtime_close();
}

bool VplImportedSurfaceTracker::validate_device(const GpuFrameCore& frame,
                                                std::string& error) const {
    if (!enforce_device_identity_) return true;
    mkvc_gpu_native_handle_desc native{};
    if (frame.get_native_handle(native, error) != MKVC_OK) return false;
    uint64_t identity = 0;
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (native.type == MKVC_GPU_NATIVE_D3D11_TEXTURE && native.handles[0] != 0) {
        reinterpret_cast<ID3D11Texture2D*>(static_cast<uintptr_t>(native.handles[0]))
            ->GetDevice(device.GetAddressOf());
        identity = reinterpret_cast<uintptr_t>(device.Get());
    }
#else
    if (native.type == MKVC_GPU_NATIVE_VA_SURFACE) identity = native.handles[0];
#endif
    if (identity == device_identity_) return true;
    error = "external Intel frame belongs to a different device/display; flush first";
    return false;
}

mkvc_result VplImportedSurfaceTracker::acquire(const std::shared_ptr<GpuFrameCore>& frame,
                                               mfxFrameSurface1*& surface, bool& imported,
                                               std::string& error) {
    surface = nullptr;
    imported = false;
    retire();
    if (!validate_device(*frame, error)) return MKVC_ERROR_INVALID_ARGUMENT;

    // Separate decode/encode sessions cannot share a SyncPoint dependency. Waiting
    // here keeps pixels resident while establishing readiness deterministically.
    mkvc_result result =
        frame->producer_completion()->wait(std::numeric_limits<uint32_t>::max(), error);
    if (result != MKVC_OK) return result;

    const auto resource = frame->backend_resource();
    if (resource.kind == BackendResourceKind::kIntelVplSurface && resource.object != nullptr) {
        surface = static_cast<mfxFrameSurface1*>(resource.object);
        return MKVC_OK;
    }
    if (inputs_.size() >= kMaxImportedInputs) {
        error = "Intel imported surface retention limit reached; flush before retrying";
        return MKVC_WOULD_BLOCK;
    }
    auto input = std::make_unique<ImportedInput>();
    result = import_vpl_encode_surface(session_, frame, surface, error);
    if (result != MKVC_OK) return result;
    input->surface = surface;
    input->owner = make_handle(frame);
    inputs_.push_back(std::move(input));
    imported = true;
    return MKVC_OK;
}

void VplImportedSurfaceTracker::retire() {
    for (auto it = inputs_.begin(); it != inputs_.end();) {
        // Output SyncPoint alone does not prove an imported surface is unused.
        // Keep our reference while checking the runtime's remaining ownership.
        if (can_release_imported_surface((*it)->surface))
            it = inputs_.erase(it);
        else
            ++it;
    }
}

void VplImportedSurfaceTracker::release_wrappers_before_runtime_close() noexcept {
    for (auto& input : inputs_) {
        if (input->surface) input->surface->FrameInterface->Release(input->surface);
        input->surface = nullptr;
    }
}

void VplImportedSurfaceTracker::release_owners_after_runtime_close() noexcept { inputs_.clear(); }

}  // namespace mkvc::gpu::intel
