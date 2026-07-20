#include "renderer.h"
#include "async_loader.h"
#include "pipeline_manager.h"
#include "render_task.h"
#include "resource_manager.h"
#include "scene.h"
#include "entity_component_system.h"
#include "primitive.h"
#include "material.h"
#include "camera.h"
#include "mesh_geometry.h"
#include "global_gpu_storage.h"
#include "frustum.h"
#include "renderer_primitive_cull_task.h"
#include "rhi/device.h"
#include "rhi/renderpass.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/descriptor_set.h"
#include "frame_resources.h"
#include "enki_task_debug.h"
#include "core/logging.h"
#include <chrono>
#include <algorithm>
#include <cstdio>

namespace ocarina {

namespace {

void bind_global_descriptor_sets(CommandBuffer& cmd, handle_ty pipeline_layout) noexcept {
    FrameResources& frame_resources = FrameResources::instance();

    if (frame_resources.has_global_ubo_descriptor_set()) {
        DescriptorSet* global_ubo_set = frame_resources.get_global_ubo_descriptor_set();
        cmd.bind_descriptor_sets(
            &global_ubo_set,
            frame_resources.global_ubo_descriptor_set_index(),
            1,
            pipeline_layout);
    } else {
        std::vector<DescriptorSet*> global_descriptor_sets =
            frame_resources.global_descriptor_sets_array();
        if (!global_descriptor_sets.empty()) {
            cmd.bind_descriptor_sets(
                global_descriptor_sets.data(),
                static_cast<uint32_t>(DescriptorSetIndex::GLOBAL_SET),
                static_cast<uint32_t>(global_descriptor_sets.size()),
                pipeline_layout);
        }
    }

    if (frame_resources.has_bindless_descriptor_set()) {
        DescriptorSet* bindless_set = frame_resources.get_bindless_descriptor_set();
        cmd.bind_descriptor_sets(
            &bindless_set,
            frame_resources.bindless_descriptor_set_index(),
            1,
            pipeline_layout);
    }
}

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
      loading_imgui_task_(*this),
      render_task_(*this)
{
    GlobalGPUStorage::instance().initialize(device);
    enki::TaskSchedulerConfig scheduler_config = task_scheduler_.GetConfig();
    scheduler_config.profilerCallbacks.threadStart = enki_thread_start_profiler_callback;
    task_scheduler_.Initialize(scheduler_config);
    PipelineManager::instance().initialize(device, &task_scheduler_);
}

Renderer::~Renderer() {
    shutdown();
    render_pass_tasks_.clear();
    ResourceManager::instance().cleanup();
}

void Renderer::shutdown() {
    if (shutdown_called_) {
        return;
    }
    shutdown_called_ = true;
    
    task_scheduler_.WaitforAllAndShutdown();
    PipelineManager::instance().shutdown();
    GlobalGPUStorage::instance().cleanup();
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
        PassGroupId::GBuffer,
        PassGroupId::Offscreen,
        PassGroupId::Lighting,
        PassGroupId::Transparent,
        PassGroupId::PostProcess,
        PassGroupId::Shadow,
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

void Renderer::update_entity_render_component(uint32_t entity_index) {
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    if (entity_index >= ecs.primitive_count()) {
        return;
    }

    Primitive& primitive = ecs.primitive(entity_index);
    RenderComponent& render_component = ecs.render_component(entity_index);
    TransformComponent& transform = ecs.transform_component(entity_index);
    primitive.initialize_render_component(device_, render_component, transform);
    primitive.update_push_constants(transform);
}

void Renderer::update_visible_render_components() {
    if (scene_ == nullptr) {
        return;
    }

    for (uint32_t entity_index : primitive_cull_task_.visible_entity_indices()) {
        update_entity_render_component(entity_index);
    }
}

void Renderer::populate_render_pass_queues(RHIRenderPass* render_pass) {
    if (render_pass == nullptr || scene_ == nullptr) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();

    auto add_entity_draw_call = [&](uint32_t entity_index) {
        if (entity_index >= ecs.primitive_count()) {
            return;
        }

        Primitive& primitive = ecs.primitive(entity_index);
        Material* material = primitive.get_material();
        if (material == nullptr) {
            return;
        }

        const PipelineState& pipeline_state = material->get_pipeline_state();
        render_pass->add_draw_call(entity_index, pipeline_state);
        PipelineManager::instance().enqueue(pipeline_state, render_pass);
        if (material->is_pipeline_dirty()) {
            material->clear_pipeline_dirty();
        }
    };

    bool cleared = false;
    for (uint32_t entity_index : primitive_cull_task_.visible_entity_indices()) {
        if (render_pass_primitive_filter_
            && !render_pass_primitive_filter_(entity_index, render_pass)) {
            continue;
        }

        if (!cleared) {
            render_pass->clear_draw_call_items();
            cleared = true;
        }

        add_entity_draw_call(entity_index);
    }
}

void Renderer::draw_render_queues(CommandBuffer& cmd, RHIRenderPass* render_pass) {
    if (render_pass == nullptr) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    VertexBuffer* vertex_buffer = GlobalGPUStorage::instance().vertex_buffer();
    IndexBuffer* index_buffer = GlobalGPUStorage::instance().index_buffer();
    if (vertex_buffer != nullptr && index_buffer != nullptr) {
        cmd.set_index_buffer(index_buffer);
    }

    for (const auto& queue : render_pass->pipeline_render_queues()) {
        const PipelineState& pipeline_state = queue.first;
        RHIPipeline* pipeline = PipelineManager::instance().get_pipeline(pipeline_state, render_pass);
        if (pipeline == nullptr) {
            continue;
        }

        cmd.bind_pipeline(pipeline);
        bind_global_descriptor_sets(cmd, pipeline->pipeline_layout);

        if (vertex_buffer != nullptr) {
            cmd.set_vertex_buffer(vertex_buffer);
        }

        for (uint32_t entity_index : queue.second->draw_call_items) {
            if (entity_index >= ecs.render_component_count()) {
                continue;
            }

            RenderComponent& item = ecs.render_component(entity_index);
            if (item.push_constant_data && item.push_constant_size > 0) {
                cmd.push_constants(item.push_constant_data, 0, item.push_constant_size);
            }

            Primitive& primitive = ecs.primitive(entity_index);
            Material* material = primitive.get_material();
            if (material != nullptr) {
                primitive.upload_material_parameters();
                if (material->has_material_descriptor_set()) {
                    DescriptorSet* material_descriptor_set = material->get_material_descriptor_set();
                    cmd.bind_descriptor_sets(
                        &material_descriptor_set,
                        material->material_descriptor_set_index(),
                        1,
                        pipeline->pipeline_layout);
                }
            }

            if (!is_valid_geometry_slice(item.geometry)) {
                continue;
            }

            if (vertex_buffer == nullptr || index_buffer == nullptr) {
                continue;
            }

            cmd.draw_indexed(
                item.geometry.index_count,
                1,
                item.geometry.index_offset,
                static_cast<int32_t>(item.geometry.vertex_offset),
                0);
        }
    }
}

void Renderer::cull_visible_primitives_parallel(Scene& scene, const Frustum& frustum) {
#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t0 = std::chrono::high_resolution_clock::now();
#endif
    primitive_cull_task_.prepare(scene.primitive_count());

    const uint32_t visible_cell_count = scene.visible_cell_count();
    if (visible_cell_count == 0) {
        primitive_cull_task_.clear_visible_entity_indices();
        return;
    }

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_prepare = std::chrono::high_resolution_clock::now();
#endif
    const size_t max_cells_per_batch = 1; // requirement: one visible cell per range

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_batch_size = std::chrono::high_resolution_clock::now();
#endif
    primitive_cull_task_.configure(
        &scene,
        &scene.visible_cell_indices(),
        visible_cell_count,
        &frustum,
        max_cells_per_batch);

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_configure = std::chrono::high_resolution_clock::now();
#endif
    task_scheduler_.AddTaskSetToPipe(&primitive_cull_task_);
    task_scheduler_.WaitforTask(&primitive_cull_task_);

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_wait = std::chrono::high_resolution_clock::now();
#endif
    primitive_cull_task_.commit_visible_results();

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_end = std::chrono::high_resolution_clock::now();
    auto ms = [](auto a, auto b) -> double {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    const double total_ms = ms(t0, t_end);
    const double prepare_ms = ms(t0, t_after_prepare);
    const double batch_size_ms = ms(t_after_prepare, t_after_batch_size);
    const double configure_ms = ms(t_after_batch_size, t_after_configure);
    const double wait_ms = ms(t_after_configure, t_after_wait);
    const double write_back_ms = ms(t_after_wait, t_end);

    (void)total_ms;
    (void)prepare_ms;
    (void)batch_size_ms;
    (void)configure_ms;
    (void)wait_ms;
    (void)write_back_ms;
#endif
}

void Renderer::cull_scene() {
    if (scene_ == nullptr || camera_ == nullptr) {
        return;
    }

    if (!frustum_culling_enabled_) {
        primitive_cull_task_.set_visible_entity_indices(scene_->entity_indices());
        return;
    }

    const math3d::Matrix4 view_projection = camera_->get_projection_matrix() * camera_->get_view_matrix();
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

void Renderer::run()
{
    if (async_loader_task_) {
        if (auto* async_loader = dynamic_cast<AsyncLoader*>(async_loader_task_)) {
            // Render passes (swapchain and/or dynamic-rendering targets) must be
            // registered before async load so PSOs can be compiled against them.
            if (async_loader->target_render_pass() == nullptr) {
                async_loader->set_target_render_pass(find_default_target_render_pass());
            }
            if (loading_progress_listener_ != nullptr) {
                async_loader->set_progress_listener(loading_progress_listener_);
            }
            if (async_complete_fn_) {
                async_loader->set_complete_callback(std::move(async_complete_fn_));
            }
        }

        task_scheduler_.AddTaskSetToPipe(async_loader_task_);

        if (async_wait_fn_) {
            async_wait_fn_();
        }

        loading_imgui_task_.configure(async_loader_task_, loading_progress_listener_);
        task_scheduler_.AddPinnedTask(&loading_imgui_task_);

        task_scheduler_.WaitforTask(async_loader_task_);
        task_scheduler_.WaitforTask(&loading_imgui_task_);

        GlobalGPUStorage::instance().finalize();

        async_loader_task_ = nullptr;
        async_wait_fn_ = nullptr;
        async_complete_fn_ = nullptr;
    }

    task_scheduler_.AddPinnedTask(&render_task_);
}

}// namespace ocarina
