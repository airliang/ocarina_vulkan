#include "renderer.h"
#include "async_loader.h"
#include "pipeline_manager.h"
#include "render_pipeline_asset.h"
#include "render_task.h"
#include "resource_manager.h"
#include "gpu_resource_thread.h"
#include "scene.h"
#include "entity_component_system.h"
#include "render_component.h"
#include "material.h"
#include "fullscreen_triangle.h"
#include "camera.h"
#include "mesh.h"
#include "resource_manager.h"
#include "global_gpu_storage.h"
#include "frustum.h"
#include "renderer_primitive_cull_task.h"
#include "rhi/device.h"
#include "rhi/renderpass.h"
#include "rhi/shader_parameters.h"
#include "rhi/shader_program.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/descriptor_set.h"
#include "frame_resources.h"
#include "enki_task_debug.h"
#include "core/profiler.h"
#include "rhi/resources/resource.h"
#include <algorithm>
#include <cstdio>

namespace ocarina {

namespace {

void enki_thread_start_profiler_callback(uint32_t thread_num) noexcept {
    char name[64] = {};
    if (thread_num == 0) {
        std::snprintf(name, sizeof(name), "enkiTS Main");
    } else {
        std::snprintf(name, sizeof(name), "enkiTS Worker %u", thread_num);
    }
    set_current_thread_name(name);
}

}// namespace

Renderer::Renderer(Device *device)
    : device_(device),
      render_task_(*this)
{
    GlobalGPUStorage::instance().initialize(device);
    FrameResources::instance().initialize(device);
    enki::TaskSchedulerConfig scheduler_config = task_scheduler_.GetConfig();
    // Need dedicated pinned threads for Render (1) and GPUResource (2), plus workers.
    scheduler_config.numTaskThreadsToCreate = std::max(scheduler_config.numTaskThreadsToCreate, 3u);
    scheduler_config.profilerCallbacks.threadStart = enki_thread_start_profiler_callback;
    task_scheduler_.Initialize(scheduler_config);
    PipelineManager::instance().initialize(device, &task_scheduler_);
    GPUResourceThread::instance().start(task_scheduler_, device);
}

Renderer::~Renderer() {
    shutdown();
    destroy_pipeline_render_passes();
    render_pass_tasks_.clear();
}

void Renderer::destroy_pipeline_render_passes() {
    for (RHIRenderPass* pass : pipeline_render_passes_) {
        if (pass == nullptr || device_ == nullptr) {
            continue;
        }
        for (auto& [id, task] : render_pass_tasks_) {
            (void)id;
            task.remove_render_pass(pass);
        }
        device_->destroy_render_pass(pass);
    }
    pipeline_render_passes_.clear();
    pipeline_render_pass_by_name_.clear();
    pipeline_asset_ = nullptr;
}

void Renderer::shutdown() {
    if (shutdown_called_) {
        return;
    }
    shutdown_called_ = true;

    GPUResourceThread::instance().request_shutdown();
    task_scheduler_.WaitforAllAndShutdown();
    GPUResourceThread::instance().shutdown();
    PipelineManager::instance().shutdown();
    GlobalGPUStorage::instance().cleanup();

    destroy_pipeline_render_passes();

    // Release GPU resources while VkDevice is still alive. Singletons / atexit
    // destructors may run after Device destruction and must not free Vulkan objects.
    // FrameResources first, then materials (and any leftover registered buffers).
    FrameResources::instance().release_gpu_buffers();
    ResourceManager::instance().cleanup();
}

void Renderer::set_render_callback(RenderCallback cb) {
    render = cb;
}

RenderPassTask& Renderer::pass_group(PassGroupId id) noexcept {
    auto [it, inserted] = render_pass_tasks_.try_emplace(id);
    if (inserted) {
        it->second.set_group_id(id);
    }
    return it->second;
}

const RenderPassTask* Renderer::find_pass_group(PassGroupId id) const noexcept {
    auto it = render_pass_tasks_.find(id);
    return it != render_pass_tasks_.end() ? &it->second : nullptr;
}

bool Renderer::has_pass_group(PassGroupId id) const noexcept {
    return find_pass_group(id) != nullptr;
}

namespace {

[[nodiscard]] RenderPassCreation make_render_pass_creation(
    const RenderPassDesc& pass,
    RenderTarget* render_target) noexcept {
    RenderPassCreation creation;
    creation.render_target = render_target;
    creation.clear_color_attachment = false;
    creation.clear_depth_attachment = false;
    // Empty color[] → depth-only (e.g. DepthPrepass): do not bind swapchain color.
    creation.enable_color_attachments = !pass.color.empty();
    creation.enable_depth_attachment = pass.depth.enabled;
    // Pipeline passes are intermediate; apps add a present pass (e.g. UI) separately.
    creation.present_swapchain = false;

    if (!pass.color.empty()) {
        const PipelineColorAttachmentDesc& color = pass.color.front();
        creation.clear_color_attachment = (color.load == PipelineLoadOp::Clear);
        if (color.has_clear) {
            creation.clear_color = color.clear;
        }
    }
    if (pass.depth.enabled) {
        creation.clear_depth_attachment = (pass.depth.load == PipelineLoadOp::Clear);
        if (pass.depth.has_clear) {
            creation.clear_depth = pass.depth.clear;
        }
    }
    return creation;
}

}// namespace

bool Renderer::load_pipeline(RenderPipelineAsset& asset, RenderTarget* render_target) {
    if (render_target == nullptr || device_ == nullptr) {
        OC_ERROR("Renderer::load_pipeline requires a non-null RenderTarget and Device");
        return false;
    }
    if (!asset.valid()) {
        OC_ERROR("Renderer::load_pipeline requires a valid RenderPipelineAsset");
        return false;
    }

    destroy_pipeline_render_passes();
    pipeline_asset_ = &asset;

    for (const RenderPassDesc& pass : asset.passes) {
        const RenderPassCreation creation = make_render_pass_creation(pass, render_target);
        RHIRenderPass* rhi_pass = device_->create_render_pass(creation);
        if (rhi_pass == nullptr) {
            OC_ERROR_FORMAT("Renderer::load_pipeline failed to create pass '{}'", pass.name.c_str());
            destroy_pipeline_render_passes();
            pipeline_asset_ = nullptr;
            return false;
        }
        pipeline_render_passes_.push_back(rhi_pass);
        pipeline_render_pass_by_name_[pass.name] = rhi_pass;
        pass_group(pass.group).add_render_pass(rhi_pass);
    }
    return true;
}

bool Renderer::load_default_pipeline(RenderTarget* render_target, const fs::path& pipeline_json_path) {
    if (!owned_default_pipeline_asset_.load_pipeline_asset(pipeline_json_path)) {
        OC_ERROR_FORMAT(
            "Renderer::load_default_pipeline failed to load '{}'",
            pipeline_json_path.string());
        return false;
    }
    return load_pipeline(owned_default_pipeline_asset_, render_target);
}

const RenderPipelineAsset* Renderer::pipeline_asset() const noexcept {
    return pipeline_asset_;
}

const RenderPassDesc* Renderer::find_pipeline_pass(std::string_view name) const noexcept {
    if (pipeline_asset_ == nullptr) {
        return nullptr;
    }
    return pipeline_asset_->find_pass(name);
}

RHIRenderPass* Renderer::find_pipeline_render_pass(std::string_view name) const noexcept {
    const ocarina_string key(name.data(), name.size());
    const auto it = pipeline_render_pass_by_name_.find(key);
    return it != pipeline_render_pass_by_name_.end() ? it->second : nullptr;
}

RHIRenderPass* Renderer::find_swapchain_render_pass() const noexcept {
    if (const RenderPassTask* ui = find_pass_group(PassGroupId::UI)) {
        if (RHIRenderPass* pass = ui->swapchain_render_pass()) {
            return pass;
        }
    }
    for (const auto& [id, task] : render_pass_tasks_) {
        (void)id;
        if (RHIRenderPass* pass = task.swapchain_render_pass()) {
            return pass;
        }
    }
    return nullptr;
}

RHIRenderPass* Renderer::find_default_target_render_pass() const noexcept {
    static constexpr PassGroupId kPreference[] = {
        PassGroupId::Opaque,
        PassGroupId::UI,
        PassGroupId::Skybox,
        PassGroupId::DepthPrepass,
        PassGroupId::GBuffer,
        PassGroupId::Offscreen,
        PassGroupId::Lighting,
        PassGroupId::Transparent,
        PassGroupId::PostProcess,
    };
    for (PassGroupId id : kPreference) {
        if (const RenderPassTask* task = find_pass_group(id)) {
            if (RHIRenderPass* pass = task->primary_render_pass()) {
                return pass;
            }
        }
    }
    for (const auto& [id, task] : render_pass_tasks_) {
        (void)id;
        if (RHIRenderPass* pass = task.primary_render_pass()) {
            return pass;
        }
    }
    return nullptr;
}

void Renderer::set_scene(Scene* scene) noexcept {
    scene_ = scene;
}


void Renderer::populate_render_pass_queues(RHIRenderPass* render_pass, PassGroupId group_id) {
    OC_PROFILE_FUNCTION;
    if (render_pass == nullptr || scene_ == nullptr) {
        return;
    }

    render_pass->clear_draw_call_items();

    EntityComponentSystem& ecs = EntityComponentSystem::instance();

    auto add_entity_draw_call = [&](uint32_t entity_index) {
        RenderComponent* render = ecs.try_render_component(entity_index);
        if (render == nullptr) {
            return;
        }

        Material* material = render->get_material();
        if (material == nullptr) {
            return;
        }

        // Enqueue only for passes listed in MaterialAsset pass_participation
        // (e.g. DepthPrepass + Opaque, or Transparent alone).
        // Legacy create_material() binds Opaque only; Offscreen RT passes reuse that binding.
        PassGroupId binding_group = group_id;
        if (!material->participates_in_pass(group_id)) {
            if (group_id == PassGroupId::Offscreen
                && material->participates_in_pass(PassGroupId::Opaque)) {
                binding_group = PassGroupId::Opaque;
            } else {
                return;
            }
        }
        const PipelineState* pipeline_state = material->pipeline_state_for_pass(binding_group);
        if (pipeline_state == nullptr) {
            return;
        }
        render_pass->add_draw_call(entity_index, *pipeline_state);
        if (material->is_pass_pipeline_dirty(binding_group)) {
            PipelineManager::instance().enqueue(*pipeline_state, render_pass);
            material->clear_pass_pipeline_dirty(binding_group);
        }
    };

    for (uint32_t entity_index : primitive_cull_task_.visible_entity_indices()) {
        if (render_pass_primitive_filter_
            && !render_pass_primitive_filter_(entity_index, render_pass)) {
            continue;
        }

        add_entity_draw_call(entity_index);
    }
}

void Renderer::draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass) {
    OC_PROFILE_FUNCTION;
    if (render_pass == nullptr) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    GlobalGPUStorage& gpu_storage = GlobalGPUStorage::instance();

    for (const auto& queue : render_pass->pipeline_render_queues()) {
        const PipelineState& pipeline_state = queue.first;
        RHIPipeline* pipeline = PipelineManager::instance().get_pipeline(pipeline_state, render_pass);
        if (pipeline == nullptr) {
            continue;
        }

        cmd.bind_pipeline(pipeline);
        FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);

        uint32_t bound_vertex_page = InvalidUI32;
        uint32_t bound_index_page = InvalidUI32;

        for (uint32_t entity_index : queue.second->draw_call_items) {
            RenderComponent* item_ptr = ecs.try_render_component(entity_index);
            if (item_ptr == nullptr) {
                continue;
            }

            RenderComponent& item = *item_ptr;
            Mesh* mesh = ResourceManager::instance().get_mesh(item.mesh_id);
            if (mesh == nullptr ||
                mesh->gpu_resource_state() != GPUResourceState::GPU_Ready) {
                continue;
            }

            const MeshGeometrySlice& geometry = mesh->geometry_slice();
            if (!is_valid_geometry_slice(geometry)) {
                continue;
            }

            VertexBuffer* vertex_buffer = gpu_storage.vertex_buffer(geometry.vertex_page);
            IndexBuffer* index_buffer = gpu_storage.index_buffer(geometry.index_page);
            if (vertex_buffer == nullptr || index_buffer == nullptr) {
                continue;
            }

            Material* material = item.get_material();
            if (material != nullptr && !material->is_renderable()) {
                continue;
            }

            if (geometry.vertex_page != bound_vertex_page) {
                cmd.set_vertex_buffer(vertex_buffer);
                bound_vertex_page = geometry.vertex_page;
            }
            if (geometry.index_page != bound_index_page) {
                cmd.set_index_buffer(index_buffer);
                bound_index_page = geometry.index_page;
            }

            ShaderProgram* program = pipeline_state.shader_program;
            if (program != nullptr) {
                item.update_push_constants_if_dirty(program, entity_index);
                if (const ocarina_vector<PushConstantRange>* ranges =
                        item.push_constants_for(program)) {
                    for (const PushConstantRange& range : *ranges) {
                        if (range.size == 0) {
                            continue;
                        }
                        cmd.push_constants(
                            range.data.data(),
                            range.offset,
                            range.size,
                            range.shader_stage);
                    }
                }
            }

            if (material != nullptr) {
                if (material->has_material_descriptor_set()) {
                    DescriptorSet* material_descriptor_set = material->get_material_descriptor_set();
                    cmd.bind_descriptor_sets(
                        &material_descriptor_set,
                        material->material_descriptor_set_index(),
                        1,
                        pipeline->pipeline_layout);
                }
            }

            cmd.draw_indexed(
                geometry.index_count,
                1,
                geometry.index_offset,
                static_cast<int32_t>(geometry.vertex_offset),
                0);
        }
    }
}

void Renderer::draw_transparents(CommandBuffer& cmd, RHIRenderPass* render_pass) {
    OC_PROFILE_FUNCTION;
    draw_render_queues(cmd, render_pass);
}

void Renderer::draw_fullscreen(
    CommandBuffer& cmd,
    Material* material,
    RHIRenderPass* render_pass,
    PassGroupId group_id) {
    OC_PROFILE_FUNCTION;
    if (material == nullptr || render_pass == nullptr || !material->is_renderable()) {
        return;
    }

    PassGroupId binding_group = group_id;
    if (!material->participates_in_pass(group_id)) {
        // create_material() binds Opaque only; skybox / post-process reuse that binding.
        if (material->participates_in_pass(PassGroupId::Opaque)) {
            binding_group = PassGroupId::Opaque;
        } else {
            return;
        }
    }

    const PipelineState* pipeline_state = material->pipeline_state_for_pass(binding_group);
    if (pipeline_state == nullptr) {
        return;
    }

    RHIPipeline* pipeline = PipelineManager::instance().get_pipeline(*pipeline_state, render_pass);
    if (pipeline == nullptr) {
        // Compile once on cache miss; do not enqueue every fullscreen draw.
        PipelineManager::instance().enqueue(*pipeline_state, render_pass);
        return;
    }

    cmd.bind_pipeline(pipeline);
    FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);

    if (material->has_material_descriptor_set()) {
        DescriptorSet* material_descriptor_set = material->get_material_descriptor_set();
        cmd.bind_descriptor_sets(
            &material_descriptor_set,
            material->material_descriptor_set_index(),
            1,
            pipeline->pipeline_layout);
    }

    FullscreenTriangle::draw(cmd);
}

void Renderer::dispatch_compute_shader(
    CommandBuffer& cmd,
    ShaderParameters& parameters,
    uint32_t group_count_x,
    uint32_t group_count_y,
    uint32_t group_count_z) {
    ShaderProgram* program = parameters.shader_program();
    Device* device = parameters.device();
    if (program == nullptr || device == nullptr
        || group_count_x == 0 || group_count_y == 0 || group_count_z == 0) {
        return;
    }

    program->ensure_compute_pipeline(device);
    RHIPipeline* pipeline = program->compute_pipeline();
    if (pipeline == nullptr) {
        return;
    }

    cmd.bind_pipeline(pipeline);
    FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);
    parameters.bind(cmd, pipeline);
    cmd.dispatch(group_count_x, group_count_y, group_count_z);
}

void Renderer::dispatch_compute_shader_for_extent(
    CommandBuffer& cmd,
    ShaderParameters& parameters,
    uint32_t width,
    uint32_t height,
    uint32_t depth) {
    ShaderProgram* program = parameters.shader_program();
    if (program == nullptr) {
        return;
    }

    const uint32_t tg_x = std::max(program->thread_group_size_x(), 1u);
    const uint32_t tg_y = std::max(program->thread_group_size_y(), 1u);
    const uint32_t tg_z = std::max(program->thread_group_size_z(), 1u);
    const uint32_t groups_x = (std::max(width, 1u) + tg_x - 1u) / tg_x;
    const uint32_t groups_y = (std::max(height, 1u) + tg_y - 1u) / tg_y;
    const uint32_t groups_z = (std::max(depth, 1u) + tg_z - 1u) / tg_z;
    dispatch_compute_shader(cmd, parameters, groups_x, groups_y, groups_z);
}

void Renderer::cull_visible_primitives_parallel(Scene& scene, const Frustum& frustum) {
    OC_PROFILE_FUNCTION;

    primitive_cull_task_.prepare(scene.primitive_count());

    const uint32_t visible_cell_count = scene.visible_cell_count();
    if (visible_cell_count == 0) {
        primitive_cull_task_.clear_visible_entity_indices();
        return;
    }

    const size_t max_cells_per_batch = 1; // requirement: one visible cell per range

    primitive_cull_task_.configure(
        &scene,
        &scene.visible_cell_indices(),
        visible_cell_count,
        &frustum,
        max_cells_per_batch);

    task_scheduler_.AddTaskSetToPipe(&primitive_cull_task_);
    task_scheduler_.WaitforTask(&primitive_cull_task_);

    primitive_cull_task_.commit_visible_results();
}

void Renderer::cull_scene() {
    if (scene_ == nullptr || camera_ == nullptr) {
        return;
    }

    if (!frustum_culling_enabled_) {
        primitive_cull_task_.set_visible_entity_indices(scene_->entity_indices());
        return;
    }

    const ktm::fmat4x4 view_projection = camera_->get_projection_matrix() * camera_->get_view_matrix();
    const Frustum frustum(view_projection);

    scene_->cull_grids(frustum);
    if (!scene_->has_grid()) {
        primitive_cull_task_.set_visible_entity_indices(scene_->entity_indices());
        return;
    }
    if (scene_->visible_cell_count() == 0) {
        primitive_cull_task_.clear_visible_entity_indices();
        return;
    }

    cull_visible_primitives_parallel(*scene_, frustum);
}

bool Renderer::is_async_loading() const noexcept {
    return async_loader_task_ != nullptr && !async_loader_task_->GetIsComplete();
}

void Renderer::poll_async_loader_completion() {
    if (async_loader_task_ == nullptr) {
        return;
    }
    if (!async_loader_task_->GetIsComplete()) {
        return;
    }

    // Complete callback runs on the render thread so set_scene / GUI switches are frame-safe.
    if (async_complete_fn_) {
        async_complete_fn_();
        async_complete_fn_ = nullptr;
    }

    async_loader_task_ = nullptr;
    async_wait_fn_ = nullptr;
}

void Renderer::start_async_loader() {
    if (async_loader_task_ == nullptr || is_async_loading()) {
        return;
    }

    if (auto* async_loader = dynamic_cast<AsyncLoader*>(async_loader_task_)) {
        // Render passes must be registered before async load so PSOs can compile.
        if (async_loader->target_render_pass() == nullptr) {
            async_loader->set_target_render_pass(find_default_target_render_pass());
        }
        if (loading_progress_listener_ != nullptr) {
            async_loader->set_progress_listener(loading_progress_listener_);
        }
        // Do not set AsyncLoader::complete_callback here — Renderer invokes
        // async_complete_fn_ on the render thread via poll_async_loader_completion().
    }

    task_scheduler_.AddTaskSetToPipe(async_loader_task_);

    if (async_wait_fn_) {
        async_wait_fn_();
    }
}

void Renderer::run()
{
    start_async_loader();

    // Start the render loop immediately so PassGroupId::UI can show loading progress
    // while the async loader runs on worker threads.
    task_scheduler_.AddPinnedTask(&render_task_);
}

}// namespace ocarina
