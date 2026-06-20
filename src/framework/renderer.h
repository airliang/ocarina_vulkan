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
#include "frustum.h"
#include "renderer_primitive_cull_task.h"
#include "entity_component_system.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace enki { class TaskScheduler; struct ITaskSet; }

namespace ocarina {
class Primitive;
class RHIRenderPass;
class Device;
class Scene;
class Camera;


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

    void set_scene(Scene* scene) noexcept;
    void set_camera(Camera* camera) noexcept { camera_ = camera; }

    void ensure_render_components(size_t count);
    [[nodiscard]] EntityComponentSystem& ecs() noexcept { return ecs_; }
    [[nodiscard]] const EntityComponentSystem& ecs() const noexcept { return ecs_; }

    void draw_opaque(CommandBuffer& cmd, RHIRenderPass* render_pass);

    void set_frustum_culling_enabled(bool enabled) noexcept { frustum_culling_enabled_ = enabled; }
    [[nodiscard]] bool frustum_culling_enabled() const noexcept { return frustum_culling_enabled_; }

    void cull_scene();
    void cull_visible_primitives_parallel(Scene& scene, const Frustum& frustum);

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

    Scene* scene_ = nullptr;
    Camera* camera_ = nullptr;
    EntityComponentSystem ecs_;
    RendererPrimitiveCullTask primitive_cull_task_;
    bool frustum_culling_enabled_ = true;

    bool shutdown_called_ = false;
};

}// namespace ocarina

