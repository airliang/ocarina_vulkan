#include "loading_imgui_task.h"
#include "renderer.h"
#include "enki_task_debug.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"

namespace ocarina {

LoadingImguiTask::LoadingImguiTask(Renderer& renderer) noexcept
    : enki::IPinnedTask(1),
      renderer_(renderer) {}

void LoadingImguiTask::configure(const enki::ICompletable* loader_task, LoadingProgressListener* progress_listener) noexcept {
    loader_task_ = loader_task;
    progress_listener_ = progress_listener;
}

void LoadingImguiTask::render_loading_frame() {
    Device* device = renderer_.device_;
    if (device == nullptr) {
        return;
    }

    if (!device->begin_frame()) {
        return;
    }

    CommandBuffer cmd = device->get_command_buffer();
    cmd.begin();

    RHIRenderPass* swapchain_pass = renderer_.find_swapchain_render_pass();

    if (swapchain_pass != nullptr) {
        if (swapchain_pass->is_swapchain_renderpass()) {
            device->attach_swapchain_semaphores(cmd);
        }

        cmd.begin_render_pass(swapchain_pass);

        if (renderer_.loading_gui_impl_) {
            renderer_.loading_gui_impl_(cmd);
        }

        cmd.end_render_pass();
    }

    cmd.end();
    device->execute_command_buffers(&cmd, 1);
    device->release_command_buffer(cmd);
    device->end_frame();
}

void LoadingImguiTask::Execute() {
    if (loader_task_ == nullptr) {
        return;
    }

    set_current_thread_name("ImGui Display Loading Progress Thread");

    enki::TaskScheduler& scheduler = renderer_.task_scheduler_;
    clock_.start();
    while (!loader_task_->GetIsComplete() && !scheduler.GetIsShutdownRequested()) {
        dt_ = clock_.elapse_s();
        clock_.start();
        render_loading_frame();
    }

    loader_task_ = nullptr;
    progress_listener_ = nullptr;
}

}// namespace ocarina
