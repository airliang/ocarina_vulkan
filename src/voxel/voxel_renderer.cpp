#include "voxel_renderer.h"
#include "voxel_world.h"
#include "framework/async_loader.h"
#include "framework/pipeline_manager.h"
#include "framework/render_pipeline_asset.h"
#include "framework/resource_manager.h"
#include "framework/gpu_resource_thread.h"
#include "framework/entity_component_system.h"
#include "framework/render_component.h"
#include "framework/material.h"
#include "framework/fullscreen_triangle.h"
#include "framework/camera.h"
#include "framework/mesh.h"
#include "framework/global_gpu_storage.h"
#include "framework/frame_resources.h"
#include "framework/enki_task_debug.h"
#include "rhi/device.h"
#include "rhi/renderpass.h"
#include "rhi/shader_program.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/descriptor_set.h"
#include "rhi/resources/resource.h"
#include "core/profiler.h"
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

[[nodiscard]] RenderPassCreation make_render_pass_creation(
    const RenderPassDesc& pass,
    RenderTarget* render_target) noexcept {
    RenderPassCreation creation;
    creation.render_target = render_target;
    creation.clear_color_attachment = false;
    creation.clear_depth_attachment = false;
    creation.enable_color_attachments = !pass.color.empty();
    creation.enable_depth_attachment = pass.depth.enabled;
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

}// namespace

VoxelRenderer::VoxelRenderer(Device* device)
    : device_(device),
      render_task_(*this) {
    GlobalGPUStorage::instance().initialize(device);
    FrameResources::instance().initialize(device);
    enki::TaskSchedulerConfig scheduler_config = task_scheduler_.GetConfig();
    scheduler_config.numTaskThreadsToCreate = std::max(scheduler_config.numTaskThreadsToCreate, 3u);
    scheduler_config.profilerCallbacks.threadStart = enki_thread_start_profiler_callback;
    task_scheduler_.Initialize(scheduler_config);
    PipelineManager::instance().initialize(device, &task_scheduler_);
    GPUResourceThread::instance().start(task_scheduler_, device);
}

VoxelRenderer::~VoxelRenderer() {
    shutdown();
    destroy_pipeline_render_passes();
    render_pass_tasks_.clear();
}

void VoxelRenderer::destroy_pipeline_render_passes() {
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

void VoxelRenderer::shutdown() {
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

    FrameResources::instance().release_gpu_buffers();
    ResourceManager::instance().cleanup();
}

RenderPassTask& VoxelRenderer::pass_group(PassGroupId id) noexcept {
    auto [it, inserted] = render_pass_tasks_.try_emplace(id);
    if (inserted) {
        it->second.set_group_id(id);
    }
    return it->second;
}

const RenderPassTask* VoxelRenderer::find_pass_group(PassGroupId id) const noexcept {
    const auto it = render_pass_tasks_.find(id);
    return it != render_pass_tasks_.end() ? &it->second : nullptr;
}

bool VoxelRenderer::has_pass_group(PassGroupId id) const noexcept {
    return find_pass_group(id) != nullptr;
}

bool VoxelRenderer::load_pipeline(RenderPipelineAsset& asset, RenderTarget* render_target) {
    if (render_target == nullptr || device_ == nullptr) {
        OC_ERROR("VoxelRenderer::load_pipeline requires a non-null RenderTarget and Device");
        return false;
    }
    if (!asset.valid()) {
        OC_ERROR("VoxelRenderer::load_pipeline requires a valid RenderPipelineAsset");
        return false;
    }

    destroy_pipeline_render_passes();
    pipeline_asset_ = &asset;

    for (const RenderPassDesc& pass : asset.passes) {
        const RenderPassCreation creation = make_render_pass_creation(pass, render_target);
        RHIRenderPass* rhi_pass = device_->create_render_pass(creation);
        if (rhi_pass == nullptr) {
            OC_ERROR_FORMAT("VoxelRenderer::load_pipeline failed to create pass '{}'", pass.name.c_str());
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

bool VoxelRenderer::load_default_pipeline(RenderTarget* render_target, const fs::path& pipeline_json_path) {
    if (!owned_default_pipeline_asset_.load_pipeline_asset(pipeline_json_path)) {
        OC_ERROR_FORMAT(
            "VoxelRenderer::load_default_pipeline failed to load '{}'",
            pipeline_json_path.string());
        return false;
    }
    return load_pipeline(owned_default_pipeline_asset_, render_target);
}

RHIRenderPass* VoxelRenderer::find_pipeline_render_pass(std::string_view name) const noexcept {
    const auto it = pipeline_render_pass_by_name_.find(ocarina_string(name));
    return it == pipeline_render_pass_by_name_.end() ? nullptr : it->second;
}

RHIRenderPass* VoxelRenderer::find_swapchain_render_pass() const noexcept {
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

RHIRenderPass* VoxelRenderer::find_default_target_render_pass() const noexcept {
    if (const RenderPassTask* opaque = find_pass_group(PassGroupId::Opaque)) {
        if (RHIRenderPass* pass = opaque->primary_render_pass()) {
            return pass;
        }
    }
    if (const RenderPassTask* ui = find_pass_group(PassGroupId::UI)) {
        if (RHIRenderPass* pass = ui->primary_render_pass()) {
            return pass;
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

void VoxelRenderer::cull_chunks() {
    visible_entity_indices_.clear();
    if (world_ == nullptr || camera_ == nullptr) {
        return;
    }

    const ktm::fmat4x4 view_projection =
        camera_->get_projection_matrix() * camera_->get_view_matrix();
    const Frustum frustum(view_projection);
    world_->cull_chunks(frustum, frustum_culling_enabled_, visible_entity_indices_);
}

void VoxelRenderer::populate_render_pass_queues(RHIRenderPass* render_pass, PassGroupId group_id) {
    OC_PROFILE_FUNCTION;
    if (render_pass == nullptr) {
        return;
    }

    render_pass->clear_draw_call_items();
    EntityComponentSystem& ecs = EntityComponentSystem::instance();

    for (uint32_t entity_index : visible_entity_indices_) {
        RenderComponent* render = ecs.try_render_component(entity_index);
        if (render == nullptr) {
            continue;
        }

        Material* material = render->get_material();
        if (material == nullptr) {
            continue;
        }

        PassGroupId binding_group = group_id;
        if (!material->participates_in_pass(group_id)) {
            if (group_id == PassGroupId::Offscreen
                && material->participates_in_pass(PassGroupId::Opaque)) {
                binding_group = PassGroupId::Opaque;
            } else {
                continue;
            }
        }
        const PipelineState* pipeline_state = material->pipeline_state_for_pass(binding_group);
        if (pipeline_state == nullptr) {
            continue;
        }
        render_pass->add_draw_call(entity_index, *pipeline_state);
        if (material->is_pass_pipeline_dirty(binding_group)) {
            PipelineManager::instance().enqueue(*pipeline_state, render_pass);
            material->clear_pass_pipeline_dirty(binding_group);
        }
    }
}

void VoxelRenderer::draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass) {
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

void VoxelRenderer::draw_transparents(CommandBuffer& cmd, RHIRenderPass* render_pass) {
    draw_render_queues(cmd, render_pass);
}

void VoxelRenderer::draw_fullscreen(
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

void VoxelRenderer::record_pass_group(
    CommandBuffer& cmd,
    PassGroupId group_id,
    RenderPassTask& pass_task,
    const RenderPassGUICallback& gui) {
    for (RHIRenderPass* render_pass : pass_task.render_passes()) {
        const bool skip_queue_populate =
            group_id == PassGroupId::Skybox
            || group_id == PassGroupId::PostProcess
            || group_id == PassGroupId::ComputePass
            || render_pass->is_compute_pass()
            || render_pass->has_execute_callback();
        if (!skip_queue_populate) {
            populate_render_pass_queues(render_pass, group_id);
        }

        if (group_id == PassGroupId::Transparent && !render_pass->has_draw_calls()) {
            continue;
        }

        if (render_pass->is_compute_pass() || group_id == PassGroupId::ComputePass) {
            if (render_pass->has_execute_callback()) {
                render_pass->execute(cmd);
            }
            continue;
        }

        if (render_pass->is_swapchain_renderpass()) {
            attach_swapchain_semaphores(device_, cmd, render_pass);
        }

        cmd.begin_render_pass(render_pass);

        if (render_pass->has_execute_callback()) {
            render_pass->execute(cmd);
        } else if (group_id == PassGroupId::Skybox) {
            if (skybox_material_ != nullptr) {
                draw_fullscreen(cmd, skybox_material_, render_pass, group_id);
            }
        } else if (group_id == PassGroupId::PostProcess) {
            if (post_process_material_ != nullptr) {
                draw_fullscreen(cmd, post_process_material_, render_pass, group_id);
            }
        } else if (group_id == PassGroupId::Transparent) {
            draw_transparents(cmd, render_pass);
        } else if (group_id != PassGroupId::UI) {
            draw_render_queues(cmd, render_pass);
        }

        if (gui && render_pass->is_swapchain_renderpass()) {
            gui(cmd);
        }

        cmd.end_render_pass();
    }
}

bool VoxelRenderer::is_async_loading() const noexcept {
    return async_loader_task_ != nullptr && !async_loader_task_->GetIsComplete();
}

void VoxelRenderer::poll_async_loader_completion() {
    if (async_loader_task_ == nullptr) {
        return;
    }
    if (!async_loader_task_->GetIsComplete()) {
        return;
    }

    if (async_complete_fn_) {
        async_complete_fn_();
        async_complete_fn_ = nullptr;
    }

    async_loader_task_ = nullptr;
    async_wait_fn_ = nullptr;
}

void VoxelRenderer::start_async_loader() {
    if (async_loader_task_ == nullptr || is_async_loading()) {
        return;
    }

    if (auto* async_loader = dynamic_cast<AsyncLoader*>(async_loader_task_)) {
        if (async_loader->target_render_pass() == nullptr) {
            async_loader->set_target_render_pass(find_default_target_render_pass());
        }
        if (loading_progress_listener_ != nullptr) {
            async_loader->set_progress_listener(loading_progress_listener_);
        }
    }

    task_scheduler_.AddTaskSetToPipe(async_loader_task_);
    if (async_wait_fn_) {
        async_wait_fn_();
    }
}

void VoxelRenderer::run() {
    start_async_loader();
    task_scheduler_.AddPinnedTask(&render_task_);
}

}// namespace ocarina
