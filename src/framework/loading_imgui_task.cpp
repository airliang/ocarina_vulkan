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

    device->begin_frame();
    CommandBuffer cmd = device->get_command_buffer();
    cmd.begin();

    RHIRenderPass* swapchain_pass = nullptr;
    for (RHIRenderPass* render_pass : renderer_.render_passes_) {
        if (render_pass != nullptr && render_pass->is_swapchain_renderpass()) {
            swapchain_pass = render_pass;
            break;
        }
    }
    if (swapchain_pass == nullptr && !renderer_.render_passes_.empty()) {
        swapchain_pass = renderer_.render_passes_.front();
    }

    if (swapchain_pass != nullptr) {
        if (swapchain_pass->is_swapchain_renderpass()) {
            cmd.add_signal_semaphore(device->get_render_complete_semaphore());
            cmd.add_wait_semaphore(device->get_present_complete_semaphore());
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
