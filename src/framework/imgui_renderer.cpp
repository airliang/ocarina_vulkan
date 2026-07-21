#include "imgui_renderer.h"
#include "sdl_window.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/imgui_creation.h"
#include "ext/imgui/imgui.h"
#include "ext/imgui/backends/imgui_impl_sdl3.h"
#include <SDL3/SDL.h>

namespace ocarina {

ImguiRenderer::ImguiRenderer(SDLWindow& window) noexcept
    : window_(&window) {
    window_->add_event_listener(this);
}

ImguiRenderer::~ImguiRenderer() noexcept {
    cleanup();
    if (window_ != nullptr) {
        window_->remove_event_listener(this);
        window_ = nullptr;
    }
}

void ImguiRenderer::init(Device& device) noexcept {
    if (initialized_ || window_ == nullptr || window_->sdl_handle() == nullptr) {
        return;
    }

    device_ = &device;

    ImguiCreation imgui_creation{};
    device.get_imgui_creation(imgui_creation);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    const float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForVulkan(window_->sdl_handle());
    device.imgui_rhi_initialize(imgui_creation);

    initialized_ = true;
}

void ImguiRenderer::cleanup() noexcept {
    if (!initialized_) {
        return;
    }

    if (device_ != nullptr) {
        device_->wait_idle();
        device_->imgui_rhi_shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void ImguiRenderer::process_sdl_event(const SDL_Event& event) noexcept {
    if (!initialized_) {
        return;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImguiRenderer::render(const CommandBuffer& command_buffer) noexcept {
    if (!initialized_ || !frame_callback_ || device_ == nullptr) {
        return;
    }

    device_->imgui_rhi_new_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    frame_callback_();
    ImGui::Render();
    device_->imgui_rhi_render_draw_data(ImGui::GetDrawData(), command_buffer.command_buffer);
}

}// namespace ocarina
