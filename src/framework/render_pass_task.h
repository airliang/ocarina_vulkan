//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "rhi/command_buffer.h"
#include "pass_group_id.h"

namespace ocarina {

class Renderer;
class Device;
class RHIRenderPass;

using RenderPassGUICallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;

class RenderPassTask : public enki::ITaskSet {
public:
    RenderPassTask() noexcept {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    explicit RenderPassTask(PassGroupId group_id) noexcept
        : RenderPassTask() {
        group_id_ = group_id;
    }

    void set_group_id(PassGroupId group_id) noexcept { group_id_ = group_id; }
    [[nodiscard]] PassGroupId group_id() const noexcept { return group_id_; }

    void configure(
        Renderer* renderer,
        Device* device,
        CommandBuffer command_buffer,
        RenderPassGUICallback render_gui) noexcept;

    void add_render_pass(RHIRenderPass* render_pass) noexcept;
    void remove_render_pass(RHIRenderPass* render_pass) noexcept;
    void clear_render_passes() noexcept { render_passes_.clear(); }

    [[nodiscard]] bool empty() const noexcept { return render_passes_.empty(); }
    [[nodiscard]] RHIRenderPass* primary_render_pass() const noexcept;
    [[nodiscard]] RHIRenderPass* swapchain_render_pass() const noexcept;
    [[nodiscard]] const std::list<RHIRenderPass*>& render_passes() const noexcept {
        return render_passes_;
    }
    [[nodiscard]] std::list<RHIRenderPass*>& render_passes() noexcept {
        return render_passes_;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

private:
    PassGroupId group_id_ = PassGroupId::Opaque;
    Renderer* renderer_ = nullptr;
    Device* device_ = nullptr;
    CommandBuffer command_buffer_;
    RenderPassGUICallback render_gui_;
    std::list<RHIRenderPass*> render_passes_;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina
