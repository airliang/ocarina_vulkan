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
#include "rhi/rendertarget.h"
#include "render_task.h"
#include "render_pass_task.h"
#include "pass_group_id.h"
#include "render_pipeline_asset.h"
#include "loading_progress_listener.h"
#include <string_view>
#include "frustum.h"
#include "renderer_primitive_cull_task.h"
#include "entity_component_system.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace enki { class TaskScheduler; struct ITaskSet; }

namespace ocarina {
class RHIRenderPass;
class Device;
class Scene;
class Camera;
class Material;
class ShaderParameters;
class RenderComponent;


class Renderer : public concepts::Noncopyable {
    friend class RenderTask;
    friend class RenderPassTask;

public:
    explicit Renderer(Device *device);
    ~Renderer();

    using RenderCallback = ocarina::function<void(double)>;
    using UpdateDescriptorPerObjectCallback = ocarina::function<void(RenderComponent&)>;
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
    /// Queue an async loader. Call start_async_loader() (or run()) to pipe it.
    /// @p complete_fn is invoked on the render thread after the loader finishes.
    void set_async_loader(enki::ITaskSet* task, ocarina::function<void()> wait_fn, AsyncLoaderCompleteCallback complete_fn = nullptr)
    {
        async_loader_task_ = task;
        async_wait_fn_ = std::move(wait_fn);
        async_complete_fn_ = std::move(complete_fn);
    }

    /// Pipe the queued async loader onto the task scheduler (safe to call after run()
    /// for mid-session reloads, e.g. Open glTF). No-ops if already loading or no task.
    void start_async_loader();

    void run();
    // Stops the render thread. Call before any main-thread Vulkan idle/teardown (e.g. ImGui shutdown).
    void shutdown();
    [[nodiscard]] double dt() const noexcept { return render_task_.last_dt(); }

    /// True while an async loader task is outstanding (not yet completed / consumed).
    [[nodiscard]] bool is_async_loading() const noexcept;

    /// Returns (and creates if missing) the RenderPassTask for the given group.
    [[nodiscard]] RenderPassTask& pass_group(PassGroupId id) noexcept;
    [[nodiscard]] const RenderPassTask* find_pass_group(PassGroupId id) const noexcept;
    [[nodiscard]] bool has_pass_group(PassGroupId id) const noexcept;

    /// Create an RHIRenderPass per pass entry in @p asset.
    /// All created passes share @p render_target (typically the swapchain).
    /// Apps can still add hard-coded passes (Skybox / UI) to other pass groups.
    bool load_pipeline(RenderPipelineAsset& asset, RenderTarget* render_target);

    /// Load the built-in ForwardPBR pipeline (DepthPrepass → Opaque → Transparent)
    /// from @p pipeline_json_path into an owned asset, then create RHI passes on
    /// @p render_target. DepthPrepass and Opaque share that target's depth buffer.
    bool load_default_pipeline(RenderTarget* render_target, const fs::path& pipeline_json_path);

    [[nodiscard]] const RenderPipelineAsset* pipeline_asset() const noexcept;
    [[nodiscard]] const RenderPassDesc* find_pipeline_pass(std::string_view name) const noexcept;
    [[nodiscard]] RHIRenderPass* find_pipeline_render_pass(std::string_view name) const noexcept;

    [[nodiscard]] const ocarina_map<PassGroupId, RenderPassTask>& pass_groups() const noexcept {
        return render_pass_tasks_;
    }
    [[nodiscard]] ocarina_map<PassGroupId, RenderPassTask>& pass_groups() noexcept {
        return render_pass_tasks_;
    }

    /// First swapchain RHIRenderPass found, preferring PassGroupId::UI.
    [[nodiscard]] RHIRenderPass* find_swapchain_render_pass() const noexcept;
    /// Default PSO / async-load target: Opaque, then UI, then first non-empty group.
    [[nodiscard]] RHIRenderPass* find_default_target_render_pass() const noexcept;

    void set_scene(Scene* scene) noexcept;
    void set_camera(Camera* camera) noexcept { camera_ = camera; }
    [[nodiscard]] Scene* scene() const noexcept { return scene_; }
    [[nodiscard]] Camera* camera() const noexcept { return camera_; }

    [[nodiscard]] EntityComponentSystem& ecs() noexcept { return EntityComponentSystem::instance(); }
    [[nodiscard]] const EntityComponentSystem& ecs() const noexcept { return EntityComponentSystem::instance(); }

    [[nodiscard]] const ocarina_vector<uint32_t>& visible_entity_indices() const noexcept {
        return primitive_cull_task_.visible_entity_indices();
    }

    void draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass);
    /// Draw alpha-blended / transparent materials previously queued on @p render_pass.
    void draw_transparents(CommandBuffer& cmd, RHIRenderPass* render_pass);
    /// Bind material pipeline + descriptors and draw a fullscreen triangle (no mesh).
    void draw_fullscreen(
        CommandBuffer& cmd,
        Material* material,
        RHIRenderPass* render_pass,
        PassGroupId group_id);

    /// Flush ShaderParameters, bind compute PSO + FRAME set + local sets, then dispatch.
    void dispatch_compute_shader(
        CommandBuffer& cmd,
        ShaderParameters& parameters,
        uint32_t group_count_x,
        uint32_t group_count_y = 1,
        uint32_t group_count_z = 1);

    /// Same as dispatch_compute_shader, covering a 2D/3D extent with the program's thread group size.
    void dispatch_compute_shader_for_extent(
        CommandBuffer& cmd,
        ShaderParameters& parameters,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1);

    /// Optional skybox material drawn by PassGroupId::Skybox via draw_fullscreen.
    void set_skybox_material(Material* material) noexcept { skybox_material_ = material; }
    [[nodiscard]] Material* skybox_material() const noexcept { return skybox_material_; }

    /// Optional fullscreen material drawn by PassGroupId::PostProcess via draw_fullscreen.
    void set_post_process_material(Material* material) noexcept { post_process_material_ = material; }
    [[nodiscard]] Material* post_process_material() const noexcept { return post_process_material_; }

    /// Fill @p render_pass draw queues from visible primitives.
    /// Materials enqueue only for PassGroupIds in pass_participation
    /// (e.g. DepthPrepass + Opaque, or Transparent alone).
    void populate_render_pass_queues(RHIRenderPass* render_pass, PassGroupId group_id);

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
    /// Poll loader completion on the render thread and invoke async_complete_fn_ once.
    void poll_async_loader_completion();
    void destroy_pipeline_render_passes();

    RenderCallback render = nullptr;
    RenderGUIImplCallback render_gui_impl_ = nullptr;
    LoadingGUIImplCallback loading_gui_impl_ = nullptr;
    LoadingProgressListener* loading_progress_listener_ = nullptr;
    float4 clear_color = {0, 0, 0, 1};

    RenderTask render_task_;
    ocarina_map<PassGroupId, RenderPassTask> render_pass_tasks_;
    /// Non-owning view of the active pipeline asset (external or owned_default_).
    RenderPipelineAsset* pipeline_asset_ = nullptr;
    /// Owned copy used by load_default_pipeline (DepthPrepass → Opaque → Transparent).
    RenderPipelineAsset owned_default_pipeline_asset_;
    /// RHIRenderPass objects owned by load_pipeline (destroyed on reload / shutdown).
    ocarina_vector<RHIRenderPass*> pipeline_render_passes_;
    ocarina_map<ocarina_string, RHIRenderPass*> pipeline_render_pass_by_name_;

protected:
    Device* device_ = nullptr;

    enki::TaskScheduler task_scheduler_;

    enki::ITaskSet* async_loader_task_ = nullptr;
    ocarina::function<void()> async_wait_fn_ = nullptr;
    AsyncLoaderCompleteCallback async_complete_fn_ = nullptr;

    Scene* scene_ = nullptr;
    Camera* camera_ = nullptr;
    Material* skybox_material_ = nullptr;
    Material* post_process_material_ = nullptr;
    RendererPrimitiveCullTask primitive_cull_task_;
    bool frustum_culling_enabled_ = true;
    RenderPassPrimitiveFilter render_pass_primitive_filter_;

    bool shutdown_called_ = false;
};

}// namespace ocarina
