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
#include "loading_progress_listener.h"
#include "loading_imgui_task.h"
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
    friend class LoadingImguiTask;

public:
    explicit Renderer(Device *device);
    ~Renderer();

    using RenderCallback = ocarina::function<void(double)>;
    using UpdateDescriptorPerObjectCallback = ocarina::function<void(Primitive&)>;
    using RenderGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using LoadingGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using RenderTaskEndCallback = ocarina::function<void()>;
    using AsyncLoaderCompleteCallback = ocarina::function<void()>;

    void set_render_callback(RenderCallback cb);
    void set_render_gui_impl_callback(RenderGUIImplCallback cb)
    {
        render_gui_impl_ = cb;
    }
    void set_loading_gui_impl_callback(LoadingGUIImplCallback cb)
    {
        loading_gui_impl_ = std::move(cb);
    }
    void set_loading_progress_listener(LoadingProgressListener* listener) noexcept
    {
        loading_progress_listener_ = listener;
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
    // The loader task and its complete callback run on an enkiTS worker thread (ITaskSet), not the main thread.
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
    [[nodiscard]] double loading_dt() const noexcept { return loading_imgui_task_.last_dt(); }
    void add_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.emplace_back(render_pass);
    }

    void remove_render_pass(RHIRenderPass *render_pass)
    {
        render_passes_.remove(render_pass);
    }

    // First registered pass (swapchain / main view). Used as the async-load PSO target
    // when the loader does not set one explicitly.
    [[nodiscard]] RHIRenderPass* primary_render_pass() const noexcept
    {
        return render_passes_.empty() ? nullptr : render_passes_.front();
    }

    [[nodiscard]] const std::list<RHIRenderPass*>& render_passes() const noexcept
    {
        return render_passes_;
    }

    void set_scene(Scene* scene) noexcept;
    void set_camera(Camera* camera) noexcept { camera_ = camera; }
    [[nodiscard]] Scene* scene() const noexcept { return scene_; }
    [[nodiscard]] Camera* camera() const noexcept { return camera_; }

    [[nodiscard]] EntityComponentSystem& ecs() noexcept { return EntityComponentSystem::instance(); }
    [[nodiscard]] const EntityComponentSystem& ecs() const noexcept { return EntityComponentSystem::instance(); }

    [[nodiscard]] const std::vector<uint32_t>& visible_entity_indices() const noexcept {
        return primitive_cull_task_.visible_entity_indices();
    }

    void draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass);
    void update_visible_render_components();
    void populate_render_pass_queues(RHIRenderPass* render_pass);

    using RenderPassPrimitiveFilter = ocarina::function<bool(uint32_t entity_index, RHIRenderPass* render_pass)>;
    void set_render_pass_primitive_filter(RenderPassPrimitiveFilter filter) {
        render_pass_primitive_filter_ = std::move(filter);
    }

    void set_frustum_culling_enabled(bool enabled) noexcept { frustum_culling_enabled_ = enabled; }
    [[nodiscard]] bool frustum_culling_enabled() const noexcept { return frustum_culling_enabled_; }

    void cull_scene();
    void cull_visible_primitives_parallel(Scene& scene, const Frustum& frustum);

    [[nodiscard]] enki::TaskScheduler& task_scheduler() noexcept { return task_scheduler_; }
    [[nodiscard]] const enki::TaskScheduler& task_scheduler() const noexcept { return task_scheduler_; }

private:
    void update_entity_render_component(uint32_t entity_index);

    RenderCallback render = nullptr;
    RenderGUIImplCallback render_gui_impl_ = nullptr;
    LoadingGUIImplCallback loading_gui_impl_ = nullptr;
    LoadingProgressListener* loading_progress_listener_ = nullptr;
    float4 clear_color = {0, 0, 0, 1};

    LoadingImguiTask loading_imgui_task_;
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
    RendererPrimitiveCullTask primitive_cull_task_;
    bool frustum_culling_enabled_ = true;
    RenderPassPrimitiveFilter render_pass_primitive_filter_;

    bool shutdown_called_ = false;
};

}// namespace ocarina
