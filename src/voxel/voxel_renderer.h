#pragma once

#include "voxel_types.h"
#include "voxel_render_task.h"
#include "framework/pass_group_id.h"
#include "framework/render_pass_task.h"
#include "framework/render_pipeline_asset.h"
#include "framework/loading_progress_listener.h"
#include "framework/frustum.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "rhi/command_buffer.h"
#include "rhi/rendertarget.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <string_view>

namespace enki {
class TaskScheduler;
struct ITaskSet;
}

namespace ocarina {

class Device;
class Camera;
class Material;
class VoxelWorld;
class RHIRenderPass;
class RenderComponent;

/// Voxel-only renderer. Same lifecycle as Renderer (run / shutdown / pass groups),
/// but culls VoxelWorld chunks on the render thread and draws merged voxel meshes
/// with voxel_mesh.vert (chunk_id push constants, no g_transforms).
class OC_VOXEL_API VoxelRenderer : public concepts::Noncopyable {
    friend class VoxelRenderTask;

public:
    explicit VoxelRenderer(Device* device);
    ~VoxelRenderer();

    using RenderCallback = ocarina::function<void(double)>;
    using RenderGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using LoadingGUIImplCallback = ocarina::function<void(const CommandBuffer& cmd_buffer)>;
    using RenderTaskEndCallback = ocarina::function<void()>;
    using AsyncLoaderCompleteCallback = ocarina::function<void()>;

    void set_render_callback(RenderCallback cb) { render_ = std::move(cb); }
    void set_render_gui_impl_callback(RenderGUIImplCallback cb) { render_gui_impl_ = std::move(cb); }
    void set_loading_gui_impl_callback(LoadingGUIImplCallback cb) {
        loading_gui_impl_ = std::move(cb);
    }
    void set_loading_progress_listener(LoadingProgressListener* listener) noexcept {
        loading_progress_listener_ = listener;
    }
    void set_render_task_end_callback(RenderTaskEndCallback cb) {
        render_task_.set_end_callback(std::move(cb));
    }
    void set_clear_color(const float4& color) { clear_color_ = color; }

    void set_async_loader(
        enki::ITaskSet* task,
        ocarina::function<void()> wait_fn,
        AsyncLoaderCompleteCallback complete_fn = nullptr) {
        async_loader_task_ = task;
        async_wait_fn_ = std::move(wait_fn);
        async_complete_fn_ = std::move(complete_fn);
    }

    void start_async_loader();
    void run();
    void shutdown();

    [[nodiscard]] double dt() const noexcept { return render_task_.last_dt(); }
    [[nodiscard]] bool is_async_loading() const noexcept;

    [[nodiscard]] RenderPassTask& pass_group(PassGroupId id) noexcept;
    [[nodiscard]] const RenderPassTask* find_pass_group(PassGroupId id) const noexcept;
    [[nodiscard]] bool has_pass_group(PassGroupId id) const noexcept;

    bool load_pipeline(RenderPipelineAsset& asset, RenderTarget* render_target);
    bool load_default_pipeline(RenderTarget* render_target, const fs::path& pipeline_json_path);

    [[nodiscard]] const RenderPipelineAsset* pipeline_asset() const noexcept { return pipeline_asset_; }
    [[nodiscard]] RHIRenderPass* find_pipeline_render_pass(std::string_view name) const noexcept;
    [[nodiscard]] RHIRenderPass* find_swapchain_render_pass() const noexcept;
    [[nodiscard]] RHIRenderPass* find_default_target_render_pass() const noexcept;

    void set_world(VoxelWorld* world) noexcept { world_ = world; }
    void set_camera(Camera* camera) noexcept { camera_ = camera; }
    [[nodiscard]] VoxelWorld* world() const noexcept { return world_; }
    [[nodiscard]] Camera* camera() const noexcept { return camera_; }

    void set_skybox_material(Material* material) noexcept { skybox_material_ = material; }
    [[nodiscard]] Material* skybox_material() const noexcept { return skybox_material_; }
    void set_post_process_material(Material* material) noexcept { post_process_material_ = material; }
    [[nodiscard]] Material* post_process_material() const noexcept { return post_process_material_; }

    void set_frustum_culling_enabled(bool enabled) noexcept { frustum_culling_enabled_ = enabled; }
    [[nodiscard]] bool frustum_culling_enabled() const noexcept { return frustum_culling_enabled_; }

    [[nodiscard]] const ocarina_vector<uint32_t>& visible_entity_indices() const noexcept {
        return visible_entity_indices_;
    }

    void cull_chunks();
    void populate_render_pass_queues(RHIRenderPass* render_pass, PassGroupId group_id);
    void draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass);
    void draw_transparents(CommandBuffer& cmd, RHIRenderPass* render_pass);
    void draw_fullscreen(
        CommandBuffer& cmd,
        Material* material,
        RHIRenderPass* render_pass,
        PassGroupId group_id);

    [[nodiscard]] enki::TaskScheduler& task_scheduler() noexcept { return task_scheduler_; }
    [[nodiscard]] const enki::TaskScheduler& task_scheduler() const noexcept {
        return task_scheduler_;
    }

private:
    void poll_async_loader_completion();
    void destroy_pipeline_render_passes();
    void record_pass_group(
        CommandBuffer& cmd,
        PassGroupId group_id,
        RenderPassTask& pass_task,
        const RenderPassGUICallback& gui);

    Device* device_ = nullptr;
    enki::TaskScheduler task_scheduler_;
    VoxelRenderTask render_task_;

    RenderCallback render_;
    RenderGUIImplCallback render_gui_impl_;
    LoadingGUIImplCallback loading_gui_impl_;
    LoadingProgressListener* loading_progress_listener_ = nullptr;
    float4 clear_color_{0, 0, 0, 1};

    ocarina_map<PassGroupId, RenderPassTask> render_pass_tasks_;
    RenderPipelineAsset* pipeline_asset_ = nullptr;
    RenderPipelineAsset owned_default_pipeline_asset_;
    ocarina_vector<RHIRenderPass*> pipeline_render_passes_;
    ocarina_map<ocarina_string, RHIRenderPass*> pipeline_render_pass_by_name_;

    enki::ITaskSet* async_loader_task_ = nullptr;
    ocarina::function<void()> async_wait_fn_;
    AsyncLoaderCompleteCallback async_complete_fn_;

    VoxelWorld* world_ = nullptr;
    Camera* camera_ = nullptr;
    Material* skybox_material_ = nullptr;
    Material* post_process_material_ = nullptr;
    bool frustum_culling_enabled_ = true;
    ocarina_vector<uint32_t> visible_entity_indices_;

    bool shutdown_called_ = false;
};

}// namespace ocarina
