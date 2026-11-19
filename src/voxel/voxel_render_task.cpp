#include "voxel_render_task.h"
#include "voxel_renderer.h"
#include "framework/pipeline_manager.h"
#include "framework/pass_group_id.h"
#include "framework/pinned_task_ids.h"
#include "framework/camera.h"
#include "framework/frame_resources.h"
#include "framework/enki_task_debug.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "core/profiler.h"
#include "TaskScheduler.h"

namespace ocarina {

VoxelRenderTask::VoxelRenderTask(VoxelRenderer& renderer) noexcept
    : enki::IPinnedTask(pinned_task_thread_num(PinnedTaskIds::RenderTask)),
      renderer_(renderer) {}

void VoxelRenderTask::Execute() {
    set_current_thread_name("Voxel Render Thread");

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

void VoxelRenderTask::render_one_frame() {
    OC_PROFILE_FUNCTION;

    PipelineManager::instance().update();
    renderer_.poll_async_loader_completion();

    const bool loading = renderer_.is_async_loading();

    if (!loading && renderer_.camera_ != nullptr) {
        renderer_.camera_->update(dt_);
    }

    if (!loading) {
        renderer_.cull_chunks();
    }

    FrameResources::instance().update_per_frame(dt_, loading ? nullptr : renderer_.camera_);

    if (renderer_.render_) {
        renderer_.render_(dt_);
        PipelineManager::instance().update();
        return;
    }

    execute_default_render_path();
}

void VoxelRenderTask::execute_default_render_path() {
    Device* device = renderer_.device_;
    if (!device->begin_frame()) {
        return;
    }

    const bool loading = renderer_.is_async_loading();

    CommandBuffer cmd = device->get_command_buffer();
    cmd.begin();

    for (auto& [group_id, pass_task] : renderer_.render_pass_tasks_) {
        if (pass_task.empty()) {
            continue;
        }
        if (loading
            && group_id != PassGroupId::UI
            && group_id != PassGroupId::Skybox
            && group_id != PassGroupId::DepthPrepass
            && group_id != PassGroupId::Opaque) {
            continue;
        }

        RenderPassGUICallback gui;
        if (group_id == PassGroupId::UI) {
            gui = loading ? renderer_.loading_gui_impl_ : renderer_.render_gui_impl_;
        }

        renderer_.record_pass_group(cmd, group_id, pass_task, gui);
    }

    cmd.end();
    device->execute_command_buffers(&cmd, 1);
    device->release_command_buffer(cmd);

    device->end_frame();
    OC_PROFILE_FRAME_MARK;
    PipelineManager::instance().update();
}

}// namespace ocarina
