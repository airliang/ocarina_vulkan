#include "render_task.h"
#include "renderer.h"
#include "pipeline_manager.h"
#include "enki_task_debug.h"
#include "camera.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"
#include "frame_resources.h"
#include "TaskScheduler.h"

namespace ocarina {

namespace {

void attach_swapchain_semaphores(Device* device, CommandBuffer& cmd) noexcept {
    device->attach_swapchain_semaphores(cmd);
}

void record_render_pass(
    Renderer& renderer,
    Device* device,
    CommandBuffer& cmd,
    RHIRenderPass* render_pass,
    const Renderer::RenderGUIImplCallback& render_gui) noexcept
{
    if (render_pass->is_swapchain_renderpass()) {
        attach_swapchain_semaphores(device, cmd);
    }

    cmd.begin_render_pass(render_pass);
    renderer.draw_render_queues(cmd, render_pass);

    if (render_gui) {
        render_gui(cmd);
    }

    cmd.end_render_pass();
}

void record_frame_command_buffer(
    Renderer& renderer,
    Device* device,
    CommandBuffer& cmd,
    const std::list<RHIRenderPass*>& render_passes,
    const Renderer::RenderGUIImplCallback& render_gui) noexcept
{
    cmd.begin();

    for (RHIRenderPass* render_pass : render_passes) {
        renderer.populate_render_pass_queues(render_pass);

        const Renderer::RenderGUIImplCallback& pass_render_gui =
            render_pass->is_swapchain_renderpass() ? render_gui : Renderer::RenderGUIImplCallback{};
        record_render_pass(renderer, device, cmd, render_pass, pass_render_gui);
    }

    cmd.end();
}

}// namespace

RenderTask::RenderTask(Renderer& renderer) noexcept
    : enki::IPinnedTask(1),
      renderer_(renderer) {}

void RenderTask::Execute() {
    set_current_thread_name("Render Thread");

    clock_.start();
    dt_ = 0.0;

    while (!renderer_.task_scheduler_.GetIsShutdownRequested()) {
        dt_ = clock_.elapse_s();
        clock_.start();

        render_one_frame();
    }

    if (end_callback_) {
        end_callback_();
    }
}

void RenderTask::render_one_frame() {
    // Kick pending PSO creates first so workers can compile while we cull / update components.
    PipelineManager::instance().update();

    if (renderer_.camera_ != nullptr) {
        renderer_.camera_->update(dt_);
    }
    renderer_.cull_scene();
    renderer_.update_visible_render_components();

    FrameResources::instance().update_per_frame(dt_);

    if (renderer_.render) {
        renderer_.render(dt_);
        // Catch any PSOs enqueued during a custom render path.
        PipelineManager::instance().update();
        return;
    }

    execute_default_render_path();
}

void RenderTask::execute_default_render_path() {
    Device* device = renderer_.device_;

    device->begin_frame();
    CommandBuffer cmd = device->get_command_buffer();

    record_frame_command_buffer(
        renderer_,
        device,
        cmd,
        renderer_.render_passes_,
        renderer_.render_gui_impl_);

    device->execute_command_buffers(&cmd, 1);
    device->release_command_buffer(cmd);
    device->end_frame();

    // Dispatch PSOs enqueued during populate_render_pass_queues this frame.
    // Creation overlaps with GPU work / next frame; we never wait here.
    PipelineManager::instance().update();
}

}// namespace ocarina
