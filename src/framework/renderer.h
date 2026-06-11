//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_pool.h"
#include "math/basic_types.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/command_buffer.h"
#include "render_task.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace enki { class TaskScheduler; struct ITaskSet; }

namespace ocarina {
class Primitive;
class RHIRenderPass;
class Device;


class Renderer : public concepts::Noncopyable {
    friend class RenderTask;

public:
    explicit Renderer(Device *device);
    ~Renderer();

    using RenderCallback = ocarina::function<void(double)>;
    using UpdateDescriptorPerObjectCallback = ocarina::function<void(Primitive&)>;
    using RenderGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using RenderTaskEndCallback = ocarina::function<void()>;
    using AsyncLoaderCompleteCallback = ocarina::function<void()>;

    void set_render_callback(RenderCallback cb);
    void set_render_gui_impl_callback(RenderGUIImplCallback cb)
    {
        render_gui_impl_ = cb;
    }
    void set_render_task_end_callback(RenderTaskEndCallback cb)
    {
        render_task_.set_end_callback(std::move(cb));
    }
    void set_clear_color(const float4& color)
    {
        clear_color = color;
    }
    // Set an async loader task set + a wait callback supplied by the application.
    // The task runs on an enkiTS worker thread (ITaskSet), not the main thread.
    void set_async_loader(enki::ITaskSet* task, ocarina::function<void()> wait_fn, AsyncLoaderCompleteCallback complete_fn = nullptr)
    {
        async_loader_task_ = task;
        async_wait_fn_ = std::move(wait_fn);
        async_complete_fn_ = std::move(complete_fn);
    }

    void run();
    // Stops the render thread. Call before any main-thread Vulkan idle/teardown (e.g. ImGui shutdown).
    void shutdown();
    [[nodiscard]] double dt() const noexcept { return render_task_.last_dt(); }
    void add_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.emplace_back(render_pass);
    }

    void remove_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.remove(render_pass);
    }

private:
    RenderCallback render = nullptr;
    RenderGUIImplCallback render_gui_impl_ = nullptr;
    float4 clear_color = {0, 0, 0, 1};

    RenderTask render_task_;

protected:
    std::list<RHIRenderPass *> render_passes_;
    Device* device_ = nullptr;

    enki::TaskScheduler task_scheduler_;

    enki::ITaskSet* async_loader_task_ = nullptr;
    ocarina::function<void()> async_wait_fn_ = nullptr;
    AsyncLoaderCompleteCallback async_complete_fn_ = nullptr;

    bool shutdown_called_ = false;
};

}// namespace ocarina

