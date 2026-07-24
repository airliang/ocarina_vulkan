#include "render_task.h"
#include "renderer.h"
#include "pipeline_manager.h"
#include "render_pass_task.h"
#include "pass_group_id.h"
#include "camera.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/graphics_descriptions.h"
#include "frame_resources.h"
#include "core/profiler.h"
#include "TaskScheduler.h"

namespace ocarina {

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
    OC_PROFILE_FUNCTION;

    // Kick pending PSO creates first so workers can compile while we cull / update components.
    PipelineManager::instance().update();

    if (renderer_.camera_ != nullptr) {
        renderer_.camera_->update(dt_);
    }

    renderer_.cull_scene();
    renderer_.update_visible_render_components();

    FrameResources::instance().update_per_frame(dt_, renderer_.camera_);

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

    if (!device->begin_frame()) {
        return;
    }

    CommandBuffer recorded_cmds[MAX_COMMAND_BUFFERS_PER_SUBMIT];
    uint32_t recorded_count = 0;

    // std::map iterates PassGroupId in numeric order (Offscreen → … → UI).
    for (auto& [group_id, record_task] : renderer_.render_pass_tasks_) {
        if (record_task.empty()) {
            continue;
        }
        if (recorded_count >= MAX_COMMAND_BUFFERS_PER_SUBMIT) {
            break;
        }

        CommandBuffer cmd = device->get_command_buffer();
        const RenderPassGUICallback gui =
            (group_id == PassGroupId::UI) ? renderer_.render_gui_impl_ : RenderPassGUICallback{};

        record_task.configure(&renderer_, device, cmd, gui);
        renderer_.task_scheduler_.AddTaskSetToPipe(&record_task);
        renderer_.task_scheduler_.WaitforTask(&record_task);

        recorded_cmds[recorded_count++] = cmd;
    }

    if (recorded_count > 0) {
        device->execute_command_buffers(recorded_cmds, recorded_count);
        for (uint32_t i = 0; i < recorded_count; ++i) {
            device->release_command_buffer(recorded_cmds[i]);
        }
    }

    device->end_frame();
    OC_PROFILE_FRAME_MARK;

    // Dispatch PSOs enqueued during populate_render_pass_queues this frame.
    // Creation overlaps with GPU work / next frame; we never wait here.
    PipelineManager::instance().update();
}

}// namespace ocarina
