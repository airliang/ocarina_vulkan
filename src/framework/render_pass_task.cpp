//
// Created by Zero on 06/06/2022.
//

#include "render_pass_task.h"
#include "enki_task_debug.h"
#include "renderer.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"
#include "material.h"
#include "core/profiler.h"

namespace ocarina {

namespace {

void attach_swapchain_semaphores(
    Device* device,
    CommandBuffer& cmd,
    RHIRenderPass* render_pass) noexcept {
    if (render_pass->clear_color_attachment()) {
        cmd.add_wait_semaphore(device->get_present_complete_semaphore());
    }
    if (render_pass->present_swapchain()) {
        cmd.add_signal_semaphore(device->get_render_complete_semaphore());
    }
}

void record_render_pass(
    Renderer& renderer,
    Device* device,
    CommandBuffer& cmd,
    RHIRenderPass* render_pass,
    PassGroupId group_id,
    const RenderPassGUICallback& render_gui) noexcept
{
    if (render_pass->is_swapchain_renderpass()) {
        attach_swapchain_semaphores(device, cmd, render_pass);
    }

    cmd.begin_render_pass(render_pass);

    if (group_id == PassGroupId::Skybox) {
        if (Material* skybox = renderer.skybox_material()) {
            renderer.draw_fullscreen(cmd, skybox, render_pass);
        }
    } else {
        renderer.draw_render_queues(cmd, render_pass);
    }

    if (render_gui) {
        render_gui(cmd);
    }

    cmd.end_render_pass();
}

/// Records all passes for this group into an already-begun command buffer.
/// FRAME set 0 is bound per pipeline via FrameResources::bind_global_descriptor_sets.
void record_pass_group(
    Renderer& renderer,
    Device* device,
    CommandBuffer& cmd,
    PassGroupId group_id,
    const std::list<RHIRenderPass*>& render_passes,
    const RenderPassGUICallback& render_gui) noexcept
{
    for (RHIRenderPass* render_pass : render_passes) {
        if (group_id != PassGroupId::Skybox) {
            renderer.populate_render_pass_queues(render_pass);
        }

        const RenderPassGUICallback& pass_render_gui =
            render_pass->is_swapchain_renderpass() ? render_gui : RenderPassGUICallback{};
        record_render_pass(renderer, device, cmd, render_pass, group_id, pass_render_gui);
    }
}

}// namespace

void RenderPassTask::configure(
    Renderer* renderer,
    Device* device,
    CommandBuffer command_buffer,
    RenderPassGUICallback render_gui) noexcept
{
    renderer_ = renderer;
    device_ = device;
    command_buffer_ = command_buffer;
    render_gui_ = std::move(render_gui);
}

void RenderPassTask::add_render_pass(RHIRenderPass* render_pass) noexcept {
    if (render_pass == nullptr) {
        return;
    }
    render_passes_.emplace_back(render_pass);
}

void RenderPassTask::remove_render_pass(RHIRenderPass* render_pass) noexcept {
    render_passes_.remove(render_pass);
}

RHIRenderPass* RenderPassTask::primary_render_pass() const noexcept {
    return render_passes_.empty() ? nullptr : render_passes_.front();
}

RHIRenderPass* RenderPassTask::swapchain_render_pass() const noexcept {
    for (RHIRenderPass* render_pass : render_passes_) {
        if (render_pass != nullptr && render_pass->is_swapchain_renderpass()) {
            return render_pass;
        }
    }
    return nullptr;
}

void RenderPassTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    OC_PROFILE_FUNCTION;

    //log_enki_task_execute_range("RenderPassTask", threadnum, range.start, range.end, execute_thread_id_);

    if (range.start != 0 || renderer_ == nullptr || device_ == nullptr) {
        return;
    }

    record_pass_group(
        *renderer_,
        device_,
        command_buffer_,
        group_id_,
        render_passes_,
        render_gui_);
}

}// namespace ocarina
