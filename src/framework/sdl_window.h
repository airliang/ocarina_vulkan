//
// Created by Zero on 2022/8/16.
//

#pragma once

#include "widgets.h"
#include "sdl_event_listener.h"
#include "rhi/resources/buffer.h"
#include <SDL3/SDL.h>

namespace ocarina {

class OC_FRAMEWORK_API SDLWindow {
public:
    using MouseButtonCallback = ocarina::function<void(int /* button */, int /* action */, float2 /* (x, y) */)>;
    using CursorPositionCallback = ocarina::function<void(float2 /* (x, y) */)>;
    using WindowSizeCallback = ocarina::function<void(uint2 /* (width, height) */)>;
    using KeyCallback = ocarina::function<void(int /* key */, int /* action */)>;
    using ScrollCallback = ocarina::function<void(float2 /* (dx, dy) */)>;
    using UpdateCallback = ocarina::function<void(double)>;
    using BeginFrame = ocarina::function<void()>;
    using EndFrame = ocarina::function<void()>;

    class FrameScope {
    public:
        explicit FrameScope(SDLWindow *window) : window_(window) {
            if (window_) {
                window_->begin_frame();
            }
        }

        ~FrameScope() {
            if (window_) {
                window_->end_frame();
            }
        }

    private:
        SDLWindow *window_{nullptr};
    };

    SDLWindow(const char *name, uint2 initial_size, bool resizable = false) noexcept;
    SDLWindow(SDLWindow &&) noexcept = delete;
    SDLWindow(const SDLWindow &) noexcept = delete;
    SDLWindow &operator=(SDLWindow &&) noexcept = delete;
    SDLWindow &operator=(const SDLWindow &) noexcept = delete;
    ~SDLWindow() noexcept;

    void init(const char *name, uint2 initial_size, bool resizable) noexcept;
    void init_widgets() noexcept;

    [[nodiscard]] Widgets *widgets() noexcept { return widgets_.get(); }
    [[nodiscard]] const Widgets *widgets() const noexcept { return widgets_.get(); }
    [[nodiscard]] double dt() const noexcept { return dt_; }
    [[nodiscard]] uint2 size() const noexcept;
    [[nodiscard]] bool should_close() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return !should_close(); }
    [[nodiscard]] uint64_t get_window_handle() const { return window_handle_; }
    [[nodiscard]] SDL_Window *sdl_handle() const noexcept { return handle_; }

    SDLWindow &set_mouse_callback(MouseButtonCallback cb) noexcept;
    SDLWindow &set_cursor_position_callback(CursorPositionCallback cb) noexcept;
    SDLWindow &set_window_size_callback(WindowSizeCallback cb) noexcept;
    SDLWindow &set_key_callback(KeyCallback cb) noexcept;
    SDLWindow &set_scroll_callback(ScrollCallback cb) noexcept;
    SDLWindow &set_begin_frame_callback(BeginFrame cb) noexcept;
    SDLWindow &set_end_frame_callback(EndFrame cb) noexcept;

    void set_background(const uchar4 *pixels, uint2 size) noexcept;
    void set_background(const uchar4 *pixels) noexcept {
        set_background(pixels, size());
    }
    void set_background(const Buffer<float4> &buffer, uint2 size) noexcept;
    void set_background(const Buffer<float4> &buffer) noexcept {
        set_background(buffer, size());
    }
    void set_clear_color(float4 color) noexcept { clear_color_ = color; }
    void set_background(const float4 *pixels, uint2 size) noexcept;
    void set_background(const float4 *pixels) noexcept {
        set_background(pixels, size());
    }

    void set_should_close() noexcept;
    void set_size(uint2 size) noexcept;
    void run(UpdateCallback &&draw) noexcept;
    void run_one_frame(UpdateCallback &&draw, double dt) noexcept;
    void run_one_frame(UpdateCallback &&draw) noexcept {
        run_one_frame(OC_FORWARD(draw), 0);
    }
    void show_window() noexcept;
    void hide_window() noexcept;

    void add_event_listener(SDLWindowEventListener* listener) noexcept;
    void remove_event_listener(SDLWindowEventListener* listener) noexcept;

private:
    void begin_frame() noexcept;
    void end_frame() noexcept;
    void process_sdl_event(const SDL_Event& event) noexcept;

    SDL_Window *handle_{nullptr};
    ocarina::list<SDLWindowEventListener*> event_listeners_;
    bool should_close_ = false;
    bool resizable_{false};

    MouseButtonCallback mouse_button_callback_;
    CursorPositionCallback cursor_position_callback_;
    WindowSizeCallback window_size_callback_;
    KeyCallback key_callback_;
    ScrollCallback scroll_callback_;
    BeginFrame begin_frame_callback_;
    EndFrame end_frame_callback_;
    float4 clear_color_{make_float4(0, 0, 0, 0)};
    Clock clock_;
    double dt_{};
    unique_ptr<Widgets> widgets_{};
    uint64_t window_handle_ = InvalidUI64;
};

}// namespace ocarina
