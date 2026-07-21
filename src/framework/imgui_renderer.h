#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "sdl_event_listener.h"

namespace ocarina {

class SDLWindow;
class Device;
class CommandBuffer;

class OC_FRAMEWORK_API ImguiRenderer : public concepts::Noncopyable, public SDLWindowEventListener {
public:
    using FrameCallback = ocarina::function<void()>;

    explicit ImguiRenderer(SDLWindow& window) noexcept;
    ~ImguiRenderer() noexcept;

    ImguiRenderer(ImguiRenderer&&) noexcept = delete;
    ImguiRenderer& operator=(ImguiRenderer&&) noexcept = delete;

    void init(Device& device) noexcept;
    void cleanup() noexcept;

    void set_frame_callback(FrameCallback cb) noexcept { frame_callback_ = std::move(cb); }

    void render(const CommandBuffer& command_buffer) noexcept;

    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    void process_sdl_event(const SDL_Event& event) noexcept override;

private:
    SDLWindow* window_ = nullptr;
    Device* device_ = nullptr;
    FrameCallback frame_callback_;
    bool initialized_ = false;
};

}// namespace ocarina
