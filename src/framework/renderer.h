//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_pool.h"
#include "rhi/params.h"
#include "rhi/graphics_descriptions.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace enki { class TaskScheduler; struct IPinnedTask; }

namespace ocarina {
class Primitive;
class RHIRenderPass;
class Device;
class CommandBuffer;


class Renderer : public concepts::Noncopyable {
public:
    Renderer(Device *device) : device_(device) { task_scheduler_.Initialize(); }
    ~Renderer();

    using UpdateFrameCallBack = ocarina::function<void(double)>;
    using RenderCallback = ocarina::function<void(double)>;
    using SetupCallback = ocarina::function<void()>;
    using ReleaseCallback = ocarina::function<void()>;
    using UpdateDescriptorPerObjectCallback = ocarina::function<void(Primitive&)>;
    using RenderGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using AsyncLoaderCompleteCallback = ocarina::function<void()>;

    void set_update_frame_callback(UpdateFrameCallBack cb)
    {
        update_frame = cb;
    }
    void set_render_callback(RenderCallback cb);
    void set_setup_callback(SetupCallback cb);
    void set_release_callback(ReleaseCallback cb);
    void set_render_gui_impl_callback(RenderGUIImplCallback cb)
    {
        render_gui_impl_ = cb;
    }
    void set_clear_color(const float4& color)
    {
        clear_color = color;
    }

    // Set an async loader pinned task + a wait callback supplied by the application.
    // The wait callback should wait for completion of this particular pinned task
    // (for example by calling task_scheduler->WaitforTask(&task) or another mechanism).
    void set_async_loader(enki::IPinnedTask* task, ocarina::function<void()> wait_fn, AsyncLoaderCompleteCallback complete_fn = nullptr)
    {
        async_loader_task_ = task;
        async_wait_fn_ = std::move(wait_fn);
        async_complete_fn_ = std::move(complete_fn);
    }

    void render_frame(double dt);
    void add_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.emplace_back(render_pass);
    }

    void remove_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.remove(render_pass);
    }

private:
    UpdateFrameCallBack update_frame = nullptr;
    SetupCallback setup = nullptr;
    RenderCallback render = nullptr;
    ReleaseCallback release = nullptr;
    RenderGUIImplCallback render_gui_impl_ = nullptr;
    float4 clear_color = {0, 0, 0, 1};
    
protected:
    std::list<RHIRenderPass *> render_passes_;
    Device* device_ = nullptr;

    // enki scheduler pointer (app owns lifetime)
    enki::TaskScheduler task_scheduler_;

    // optional pinned task + wait function (app supplies wait function)
    enki::IPinnedTask* async_loader_task_ = nullptr;
    ocarina::function<void()> async_wait_fn_ = nullptr;
    AsyncLoaderCompleteCallback async_complete_fn_ = nullptr;
    
};

}// namespace ocarina