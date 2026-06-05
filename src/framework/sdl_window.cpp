//
// Created by Zero on 2022/8/16.
//

#include "sdl_window.h"
#include "imgui_widgets.h"
#include "core/logging.h"
#include "rhi/common.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ocarina {

void SDLWindow::init_widgets() noexcept {
    widgets_ = make_unique<ImGuiWidgets>(this);
}

void SDLWindow::init(const char *name, uint2 initial_size, bool resizable) noexcept {
    resizable_ = resizable;
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    flags |= resizable ? SDL_WINDOW_RESIZABLE : 0;
    handle_ = SDL_CreateWindow(name, initial_size.x, initial_size.y, flags);
#if defined(_WIN32)
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(handle_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    window_handle_ = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
#endif
    init_widgets();
}

SDLWindow::SDLWindow(const char *name, uint2 initial_size, bool resizable) noexcept {
    init(name, initial_size, resizable);
}

SDLWindow::~SDLWindow() noexcept {
    if (handle_ != nullptr) {
        SDL_DestroyWindow(handle_);
        handle_ = nullptr;
    }
}

uint2 SDLWindow::size() const noexcept {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(handle_, &width, &height);
    return make_uint2(static_cast<uint>(width), static_cast<uint>(height));
}

bool SDLWindow::should_close() const noexcept {
    return should_close_;
}

void SDLWindow::set_background(const uchar4 *pixels, uint2 size) noexcept {
    (void)pixels;
    (void)size;
}

void SDLWindow::set_background(const float4 *pixels, uint2 size) noexcept {
    (void)pixels;
    (void)size;
}

void SDLWindow::set_background(const Buffer<ocarina::float4> &buffer, ocarina::uint2 size) noexcept {
    (void)buffer;
    (void)size;
}

void SDLWindow::set_should_close() noexcept {
    should_close_ = true;
}

SDLWindow &SDLWindow::set_mouse_callback(MouseButtonCallback cb) noexcept {
    mouse_button_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_cursor_position_callback(CursorPositionCallback cb) noexcept {
    cursor_position_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_window_size_callback(WindowSizeCallback cb) noexcept {
    window_size_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_key_callback(KeyCallback cb) noexcept {
    key_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_scroll_callback(ScrollCallback cb) noexcept {
    scroll_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_begin_frame_callback(BeginFrame cb) noexcept {
    begin_frame_callback_ = std::move(cb);
    return *this;
}

SDLWindow &SDLWindow::set_end_frame_callback(EndFrame cb) noexcept {
    end_frame_callback_ = std::move(cb);
    return *this;
}

void SDLWindow::add_event_listener(SDLWindowEventListener* listener) noexcept {
    if (listener == nullptr) {
        return;
    }
    for (SDLWindowEventListener* existing : event_listeners_) {
        if (existing == listener) {
            return;
        }
    }
    event_listeners_.push_back(listener);
}

void SDLWindow::remove_event_listener(SDLWindowEventListener* listener) noexcept {
    event_listeners_.remove(listener);
}

void SDLWindow::process_sdl_event(const SDL_Event& event) noexcept {
    for (SDLWindowEventListener* listener : event_listeners_) {
        if (listener != nullptr) {
            listener->process_sdl_event(event);
        }
    }
}

void SDLWindow::begin_frame() noexcept {
    SDL_Event windowEvent;
    while (SDL_PollEvent(&windowEvent)) {
        process_sdl_event(windowEvent);
        if (windowEvent.type == SDL_EVENT_QUIT) {
            should_close_ = true;
            break;
        }
        if (windowEvent.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            should_close_ = true;
            break;
        }
        if (windowEvent.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            break;
        }
    }
    if (begin_frame_callback_) {
        begin_frame_callback_();
    }
}

void SDLWindow::end_frame() noexcept {
    if (end_frame_callback_) {
        end_frame_callback_();
    }
}

void SDLWindow::run_one_frame(UpdateCallback &&draw, double dt) noexcept {
    begin_frame();
    draw(dt);
    end_frame();
}

void SDLWindow::run(UpdateCallback &&draw) noexcept {
    while (!should_close()) {
        clock_.begin();
        run_one_frame(OC_FORWARD(draw), dt_);
        dt_ = clock_.elapse_s();
    }
}

void SDLWindow::set_size(uint2 size) noexcept {
    if (resizable_) {
        SDL_SetWindowSize(handle_, static_cast<int>(size.x), static_cast<int>(size.y));
    } else {
        OC_WARNING("Ignoring resize on non-resizable window.");
    }
}

void SDLWindow::show_window() noexcept {
    SDL_ShowWindow(handle_);
}

void SDLWindow::hide_window() noexcept {
    SDL_HideWindow(handle_);
}

}// namespace ocarina
