//
// Created by Zero on 2022/8/16.
//

#include "sdl_window.h"
#include "widgets.h"
#include "core/logging.h"
#include "rhi/common.h"
#include "ext/imgui/backends/imgui_impl_sdl3.h"
#include "ext/imgui/backends/imgui_impl_vulkan.h"
#include "rhi/device.h"
#include "rhi/imgui_creation.h"

namespace ocarina {


void SDLWindow::init_widgets() noexcept {
    widgets_ = make_unique<ImGuiWidgets>(this);
}

void SDLWindow::init(const char *name, uint2 initial_size, bool resizable) noexcept {
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    flags |= resizable ? SDL_WINDOW_RESIZABLE : 0;
    handle_ = SDL_CreateWindow(name, initial_size.x, initial_size.y, flags);
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(handle_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    window_handle_ = (uint64_t)hwnd;
    IMGUI_CHECKVERSION();
    init_widgets();
}

SDLWindow::SDLWindow(const char *name, uint2 initial_size, bool resizable) noexcept
    : Window(resizable) {
    init(name, initial_size, resizable);
}

SDLWindow::~SDLWindow() noexcept {

    SDL_DestroyWindow(handle_);
}

uint2 SDLWindow::size() const noexcept {
    auto width = 0;
    auto height = 0;
    //glfwGetWindowSize(handle_, &width, &height);
    SDL_GetWindowSize(handle_, &width, &height);
    return make_uint2(
        static_cast<uint>(width),
        static_cast<uint>(height));
}

bool SDLWindow::should_close() const noexcept {
    return should_close_;
}

void SDLWindow::set_background(const uchar4 *pixels, uint2 size) noexcept {
    //if (texture_ == nullptr) { texture_ = ocarina::make_unique<GLTexture>(); }
    //texture_->load(pixels, size);
}

void SDLWindow::set_background(const float4 *pixels, uint2 size) noexcept {
    //if (texture_ == nullptr) {
    //    texture_ = ocarina::make_unique<GLTexture>();
    //}
    //texture_->load(pixels, size);
}

void SDLWindow::gen_buffer(ocarina::uint &handle, ocarina::uint size_in_byte) const noexcept {
    //CHECK_GL(glGenBuffers(1, addressof(handle)));
    //CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, handle));
    //CHECK_GL(glBufferData(GL_ARRAY_BUFFER, size_in_byte,
    //                      nullptr, GL_STREAM_DRAW));
    //CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, 0u));
}

void SDLWindow::bind_buffer(ocarina::uint &handle, ocarina::uint size_in_byte) const noexcept {
    //if (handle == 0) {
    //    gen_buffer(handle, size_in_byte);
    //}
    //CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, handle));
}

void SDLWindow::set_background(const Buffer<ocarina::float4> &buffer, ocarina::uint2 size) noexcept {
    //if (texture_ == nullptr) {
    //    texture_ = ocarina::make_unique<GLTexture>();
    //}
    //texture_->bind();
    //bind_buffer(buffer.gl_handle(), buffer.size_in_byte());
}

void SDLWindow::unbind_buffer(ocarina::uint &handle) const noexcept {
    CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void SDLWindow::set_should_close() noexcept {
    should_close_ = true;
}

void SDLWindow::_begin_frame() noexcept {
    //if (!should_close()) {
        SDL_Event windowEvent;
        while (SDL_PollEvent(&windowEvent)) {
            if (imgui_initialized_)
            {
                ImGui_ImplSDL3_ProcessEvent(&windowEvent);
            }
            if (windowEvent.type == SDL_EVENT_QUIT) {
                should_close_ = true;
                break;
            }
            else if (windowEvent.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                break;
            }
        }
        Window::_begin_frame();
    //}
        
}

void SDLWindow::_end_frame() noexcept {
    if (!should_close()) {
        Window::_end_frame();
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

void SDLWindow::hide_window() noexcept
{
    SDL_HideWindow(handle_);
}

void SDLWindow::init_imgui(const ImguiCreation* imgui_creation) noexcept {
    static ImGui_ImplVulkanH_Window g_MainWindowData;
    imgui_creation_ = *imgui_creation;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    int w, h;
    SDL_GetWindowSize(handle_, &w, &h);
    //setup_vulkan_window(imgui_creation, wd, reinterpret_cast<VkSurfaceKHR>(imgui_creation->surface_), w, h);

    ImGui_ImplSDL3_InitForVulkan(handle_);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    init_info.Instance = reinterpret_cast<VkInstance>(imgui_creation->instance_);
    init_info.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(imgui_creation->physical_device_);
    init_info.Device = reinterpret_cast<VkDevice>(imgui_creation->device_);
    init_info.QueueFamily = imgui_creation->queue_family_;
    init_info.Queue = reinterpret_cast<VkQueue>(imgui_creation->queue_);
    init_info.PipelineCache = reinterpret_cast<VkPipelineCache>(imgui_creation->pipeline_cache_);
    init_info.DescriptorPool = reinterpret_cast<VkDescriptorPool>(imgui_creation->descriptor_pool_);
    init_info.MinImageCount = imgui_creation->image_count_;
    init_info.ImageCount = imgui_creation->image_count_;
    init_info.Allocator = reinterpret_cast<VkAllocationCallbacks*>(imgui_creation->allocator_callback_);
    init_info.PipelineInfoMain.RenderPass = reinterpret_cast<VkRenderPass>(imgui_creation->swapchain_render_pass_);
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = nullptr;//reinterpret_cast<void*>(imgui_creation->check_VkResult_fn_);
    ImGui_ImplVulkan_Init(&init_info);
    imgui_initialized_ = true;
}
/*
void SDLWindow::setup_vulkan_window(const ImguiCreation* imgui_creation, ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
    // Check for WSI support
    VkBool32 res;
    VkPhysicalDevice physical_device = reinterpret_cast<VkPhysicalDevice>(imgui_creation->physical_device_);
    vkGetPhysicalDeviceSurfaceSupportKHR(reinterpret_cast<VkPhysicalDevice>(physical_device, imgui_creation->queue_family_, surface, &res);
    if (res != VK_TRUE)
    {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physical_device, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical_device, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));
    //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

    // Create SwapChain, RenderPass, Framebuffer, etc.
    OC_ASSERT(imgui_creation->image_count_ >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(reinterpret_cast<VkInstance>(imgui_creation->instance_), physical_device, reinterpret_cast<VkDevice>(imgui_creation->device_), wd, imgui_creation->queue_family_,
        reinterpret_cast<VkAllocationCallbacks*>(imgui_creation->allocator_callback_), width, height, imgui_creation->image_count_, 0);
}
*/

void SDLWindow::cleanup_imgui() noexcept {
    if (!imgui_initialized_) {
        return;
    }
    vkDeviceWaitIdle(reinterpret_cast<VkDevice>(imgui_creation_.device_));
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    
    ImGui::DestroyContext();
    imgui_initialized_ = false;
}

void SDLWindow::render_gui(const ImguiFrameInfo& imgui_frame) noexcept
{
    if (imgui_frame_callback_ != nullptr)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        VkCommandBuffer vk_command_buffer = reinterpret_cast<VkCommandBuffer>(imgui_frame.command_buffer_);

        VkCommandBufferBeginInfo info_cmd{};
        info_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(vk_command_buffer, &info_cmd);

        VkRenderPassBeginInfo renderPassBeginInfo{};
        VkClearValue clear_values[2];
        clear_values[0] = { imgui_frame.clear_color_.x, imgui_frame.clear_color_.y, imgui_frame.clear_color_.z, imgui_frame.clear_color_.w };
        clear_values[1].depthStencil = { imgui_frame.clear_depth_, imgui_frame.clear_stencil_ };
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = reinterpret_cast<VkRenderPass>(imgui_creation_.swapchain_render_pass_);
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = imgui_frame.framebuffer_size_.x;
        renderPassBeginInfo.renderArea.extent.height = imgui_frame.framebuffer_size_.y;
        renderPassBeginInfo.clearValueCount = 0;// imgui_creation_.image_count_;
        renderPassBeginInfo.pClearValues = nullptr;//clear_values;
        renderPassBeginInfo.framebuffer = reinterpret_cast<VkFramebuffer>(imgui_frame.framebuffer_);
        vkCmdBeginRenderPass(vk_command_buffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        imgui_frame_callback_();
        bool show_demo_window = true;
        ImGui::ShowDemoWindow(&show_demo_window);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk_command_buffer);
        

        vkCmdEndRenderPass(vk_command_buffer);

        vkEndCommandBuffer(vk_command_buffer);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &vk_command_buffer;
        vkQueueSubmit(reinterpret_cast<VkQueue>(imgui_creation_.queue_), 1, &submit_info, VK_NULL_HANDLE);
    }
}

void SDLWindow::render_gui(handle_ty command_buffer) noexcept
{
    if (imgui_frame_callback_ != nullptr)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        VkCommandBuffer vk_command_buffer = reinterpret_cast<VkCommandBuffer>(command_buffer);

        imgui_frame_callback_();
        bool show_demo_window = true;
        ImGui::ShowDemoWindow(&show_demo_window);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk_command_buffer);
    }
}

}// namespace ocarina

OC_EXPORT_API ocarina::SDLWindow *create_sdlwindow(const char *name, ocarina::uint2 initial_size, bool resizable) {
    return ocarina::new_with_allocator<ocarina::SDLWindow>(name, initial_size, resizable);
}

OC_EXPORT_API void destroy_sdlwindow(ocarina::SDLWindow *ptr) {
    ocarina::delete_with_allocator(ptr);
}