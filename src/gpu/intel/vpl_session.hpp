#pragma once

#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>

namespace mkvc::gpu::intel {

/**
 * @brief Own a oneVPL loader, session, and optional codec component.
 *
 * Destruction follows the required component -> session -> loader order. The
 * object is intentionally non-copyable; decoded GPU frames may share an owning
 * pointer to extend the oneVPL runtime beyond their capture object.
 */
class VplSession final {
   public:
    enum class Component { kNone, kDecode, kEncode };

    VplSession() = default;
    VplSession(const VplSession&) = delete;
    VplSession& operator=(const VplSession&) = delete;
    ~VplSession() { reset(); }

    /** @brief Load the oneVPL dispatcher. */
    bool load() noexcept {
        if (loader_ != nullptr) return true;
        loader_ = MFXLoad();
        return loader_ != nullptr;
    }

    /** @brief Create the selected implementation's session. */
    mfxStatus create_session(mfxU32 implementation = 0) noexcept {
        if (loader_ == nullptr || session_ != nullptr) return MFX_ERR_UNDEFINED_BEHAVIOR;
        return MFXCreateSession(loader_, implementation, &session_);
    }

    /** @brief Record that a codec component completed initialization. */
    void mark_initialized(Component component) noexcept { component_ = component; }

    /** @brief Close only the initialized codec component, if any. */
    void close_component() noexcept {
        if (session_ != nullptr && component_ == Component::kDecode)
            (void)MFXVideoDECODE_Close(session_);
        else if (session_ != nullptr && component_ == Component::kEncode)
            (void)MFXVideoENCODE_Close(session_);
        component_ = Component::kNone;
    }

    /** @brief Close the component and release the session and loader. */
    void reset() noexcept {
        close_component();
        if (session_ != nullptr) (void)MFXClose(session_);
        if (loader_ != nullptr) MFXUnload(loader_);
        session_ = nullptr;
        loader_ = nullptr;
    }

    /** @return borrowed dispatcher loader handle. */
    mfxLoader loader() const noexcept { return loader_; }

    /** @return borrowed oneVPL session handle. */
    mfxSession session() const noexcept { return session_; }

   private:
    mfxLoader loader_ = nullptr;
    mfxSession session_ = nullptr;
    Component component_ = Component::kNone;
};

}  // namespace mkvc::gpu::intel
