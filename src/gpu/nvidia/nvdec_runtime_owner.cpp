#include "nvdec_runtime_owner.hpp"

#include <utility>

#include "nvdec_api.hpp"
#include "nvdec_runtime_cleanup.hpp"

namespace mkvc::gpu::nvidia {

NvdecRuntimeOwner::NvdecRuntimeOwner(NvdecParserRuntime runtime)
    : api_(std::move(runtime.api)), context_(runtime.context), parser_(runtime.parser) {}

NvdecRuntimeOwner::~NvdecRuntimeOwner() {
    std::string ignored;
    (void)close(ignored);
}

void NvdecRuntimeOwner::acquire_mapping() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++outstanding_mappings_;
}

bool NvdecRuntimeOwner::destroy_decoder_if_ready(std::string& error) noexcept {
    if (!closed_ || outstanding_mappings_ != 0 || context_ == nullptr) return true;
    return destroy_nvdec_decoder_context(*api_, context_, decoder_, error);
}

void NvdecRuntimeOwner::release_mapping(unsigned long long device_pointer) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string ignored;
    (void)release_nvdec_mapping(*api_, context_, decoder_, device_pointer, ignored);
    if (outstanding_mappings_ != 0) --outstanding_mappings_;
    (void)destroy_decoder_if_ready(ignored);
}

bool NvdecRuntimeOwner::close(std::string& error) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return true;
    bool success = destroy_nvdec_parser(*api_, context_, parser_, error);
    closed_ = true;
    if (!destroy_decoder_if_ready(error)) success = false;
    return success;
}

uint32_t NvdecRuntimeOwner::outstanding_mappings() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return outstanding_mappings_;
}

}  // namespace mkvc::gpu::nvidia
