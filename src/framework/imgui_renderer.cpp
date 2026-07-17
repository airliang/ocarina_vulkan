#include "imgui_renderer.h"
#include "sdl_window.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "ext/imgui/imgui.h"
#include "ext/imgui/backends/imgui_impl_sdl3.h"
#include "ext/imgui/backends/imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

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
    device.get_imgui_creation(imgui_creation_);

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

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = imgui_creation_.api_version_ != 0
        ? imgui_creation_.api_version_
        : VK_API_VERSION_1_3;
    init_info.Instance = reinterpret_cast<VkInstance>(imgui_creation_.instance_);
    init_info.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(imgui_creation_.physical_device_);
    init_info.Device = reinterpret_cast<VkDevice>(imgui_creation_.device_);
    init_info.QueueFamily = imgui_creation_.queue_family_;
    init_info.Queue = reinterpret_cast<VkQueue>(imgui_creation_.queue_);
    init_info.PipelineCache = reinterpret_cast<VkPipelineCache>(imgui_creation_.pipeline_cache_);
    init_info.DescriptorPool = reinterpret_cast<VkDescriptorPool>(imgui_creation_.descriptor_pool_);
    init_info.MinImageCount = imgui_creation_.image_count_;
    init_info.ImageCount = imgui_creation_.image_count_;
    init_info.Allocator = reinterpret_cast<VkAllocationCallbacks*>(imgui_creation_.allocator_callback_);
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = nullptr;

    VkFormat color_format = static_cast<VkFormat>(imgui_creation_.color_attachment_format_);
    if (imgui_creation_.use_dynamic_rendering_) {
        init_info.UseDynamicRendering = true;
        init_info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        VkPipelineRenderingCreateInfoKHR rendering_info = {};
        rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachmentFormats = &color_format;
        rendering_info.depthAttachmentFormat = static_cast<VkFormat>(imgui_creation_.depth_attachment_format_);
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering_info;
#endif
    } else {
        init_info.UseDynamicRendering = false;
        init_info.PipelineInfoMain.RenderPass =
            reinterpret_cast<VkRenderPass>(imgui_creation_.swapchain_render_pass_);
    }

    ImGui_ImplVulkan_Init(&init_info);

    initialized_ = true;
}

void ImguiRenderer::cleanup() noexcept {
    if (!initialized_) {
        return;
    }

    if (device_ != nullptr) {
        device_->wait_idle();
    }
    ImGui_ImplVulkan_Shutdown();
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
    if (!initialized_ || !frame_callback_) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    frame_callback_();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(),
        reinterpret_cast<VkCommandBuffer>(command_buffer.command_buffer));
}

}// namespace ocarina
