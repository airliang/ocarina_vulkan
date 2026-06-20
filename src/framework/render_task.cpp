#include "render_task.h"
#include "renderer.h"
#include "camera.h"
#include "cmd_record_task.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/pipeline_state.h"
#include "rhi/renderpass.h"
#include "frame_resources.h"
#include "TaskScheduler.h"

namespace ocarina {

namespace {

void record_scene_drawing(
    Renderer& renderer,
    Device* device,
    CommandBuffer* cmd,
    const std::list<RHIRenderPass*>& render_passes,
    bool leave_swapchain_pass_open_for_imgui) noexcept
{
    cmd->begin();

    std::vector<DescriptorSet*> global_descriptor_sets =
        FrameResources::instance().global_descriptor_sets_array();

    for (RHIRenderPass* render_pass : render_passes) {
        if (render_pass->is_swapchain_renderpass()) {
            cmd->add_signal_semaphore(device->get_render_complete_semaphore());
            cmd->add_wait_semaphore(device->get_present_complete_semaphore());
        }

        cmd->begin_render_pass(render_pass);

        const auto& queues = render_pass->pipeline_render_queues();
        if (!queues.empty()) {
            RHIPipeline* pipeline = queues.begin()->first;
            FrameResources& frame_resources = FrameResources::instance();

            if (frame_resources.has_global_ubo_descriptor_set()) {
                DescriptorSet* global_ubo_set = frame_resources.get_global_ubo_descriptor_set();
                cmd->bind_descriptor_sets(
                    &global_ubo_set,
                    frame_resources.global_ubo_descriptor_set_index(),
                    1,
                    pipeline->pipeline_layout);
            } else if (!global_descriptor_sets.empty()) {
                cmd->bind_descriptor_sets(
                    global_descriptor_sets.data(),
                    static_cast<uint32_t>(DescriptorSetIndex::GLOBAL_SET),
                    static_cast<uint32_t>(global_descriptor_sets.size()),
                    pipeline->pipeline_layout);
            }

            if (frame_resources.has_bindless_descriptor_set()) {
                DescriptorSet* bindless_set = frame_resources.get_bindless_descriptor_set();
                cmd->bind_descriptor_sets(
                    &bindless_set,
                    frame_resources.bindless_descriptor_set_index(),
                    1,
                    pipeline->pipeline_layout);
            }
        }

        renderer.draw_opaque(*cmd, render_pass);

        const bool leave_pass_open =
            leave_swapchain_pass_open_for_imgui && render_pass->is_swapchain_renderpass();
        if (!leave_pass_open) {
            cmd->end_render_pass();
        }
    }
}

}// namespace

RenderTask::RenderTask(Renderer& renderer) noexcept
    : enki::IPinnedTask(1),
      renderer_(renderer) {}

void RenderTask::Execute() {
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
    if (renderer_.camera_ != nullptr) {
        renderer_.camera_->update(dt_);
    }
    renderer_.cull_scene();

    FrameResources::instance().update_per_frame(dt_);

    if (renderer_.render) {
        renderer_.render(dt_);
        return;
    }

    execute_default_render_path();
}

void RenderTask::execute_default_render_path() {
    Device* device = renderer_.device_;
    const bool imgui_pending = renderer_.render_gui_impl_ != nullptr;

    device->begin_frame();
    CommandBuffer cmd = device->get_command_buffer();

    CmdRecordTask record_task(
        device,
        cmd,
        [renderer = &renderer_, render_passes = renderer_.render_passes_, imgui_pending](Device* dev, CommandBuffer* cb) {
            record_scene_drawing(*renderer, dev, cb, render_passes, imgui_pending);
        });
    renderer_.task_scheduler_.AddTaskSetToPipe(&record_task);
    renderer_.task_scheduler_.WaitforTask(&record_task);

    if (imgui_pending) {
        renderer_.render_gui_impl_(cmd);
        cmd.end_render_pass();
    }

    cmd.end();
    device->execute_command_buffers(&cmd, 1);
    device->release_command_buffer(cmd);
    device->end_frame();
}

}// namespace ocarina
