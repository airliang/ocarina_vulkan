//
// Created by Zero on 2022/8/16.
//

#pragma once

#include "GUI/window.h"
#include "widgets.h"
#include <SDL3/SDL.h>
#include "rhi/imgui_creation.h"

namespace ocarina {
class SDLWindow : public Window {
private:
    //ocarina::shared_ptr<GLFWContext> context_;
    SDL_Window *handle_{nullptr};
    //mutable ocarina::unique_ptr<GLTexture> texture_;
    ImguiCreation imgui_creation_{};
private:
    void _begin_frame() noexcept override;
    void _end_frame() noexcept override;
    bool should_close_ = false;
    bool imgui_initialized_ = false;
    //void setup_vulkan_window(const ImguiCreation* imgui_creation, ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) noexcept;
public:
    SDLWindow(const char *name, uint2 initial_size, bool resizable = false) noexcept;
    void init(const char *name, uint2 initial_size, bool resizable) noexcept override;
    void init_widgets() noexcept override;
    SDLWindow(SDLWindow &&) noexcept = delete;
    SDLWindow(const SDLWindow &) noexcept = delete;
    SDLWindow &operator=(SDLWindow &&) noexcept = delete;
    SDLWindow &operator=(const SDLWindow &) noexcept = delete;
    void full_screen() override {}
    void swap_monitor() override {}
    ~SDLWindow() noexcept override;
    [[nodiscard]] uint2 size() const noexcept override;
    [[nodiscard]] bool should_close() const noexcept override;
    [[nodiscard]] auto handle() const noexcept { return handle_; }
    void set_background(const uchar4 *pixels, uint2 size) noexcept override;
    void set_background(const float4 *pixels, uint2 size) noexcept override;
    void gen_buffer(ocarina::uint &handle, ocarina::uint size_in_byte) const noexcept override;
    void bind_buffer(ocarina::uint &handle, ocarina::uint size_in_byte) const noexcept override;
    void unbind_buffer(ocarina::uint &handle) const noexcept override;
    void set_background(const Buffer<ocarina::float4> &buffer, ocarina::uint2 size) noexcept override;
    void set_should_close() noexcept override;
    void set_size(uint2 size) noexcept override;
    void show_window() noexcept override;
    void hide_window() noexcept override;
    void init_imgui(const ImguiCreation* imgui_creation) noexcept override;
    void cleanup_imgui() noexcept override;
    void render_gui(const ImguiFrameInfo& imgui_frame) noexcept override;
    void render_gui(handle_ty command_buffer) noexcept override;
};
}// namespace ocarina