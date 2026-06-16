//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "renderer.h"
#include "render_task.h"
#include "resource_manager.h"
#include "scene.h"
#include "camera.h"
#include "frustum.h"
#include "renderer_primitive_cull_task.h"
#include "rhi/device.h"
#include <algorithm>

namespace ocarina {

Renderer::Renderer(Device *device)
    : device_(device),
      render_task_(*this)
{
    task_scheduler_.Initialize();
}

Renderer::~Renderer() {
    shutdown();
    render_passes_.clear();
    ResourceManager::instance().cleanup();
}

void Renderer::shutdown() {
    if (shutdown_called_) {
        return;
    }
    shutdown_called_ = true;
    task_scheduler_.WaitforAllAndShutdown();
}

void Renderer::set_render_callback(RenderCallback cb) {
    render = cb;
}

void Renderer::cull_visible_primitives_parallel(Scene& scene, const Frustum& frustum) {
    scene.build_primitive_cull_batch();
    const std::vector<uint32_t>& primitive_batch = scene.primitive_cull_batch();

    primitive_cull_task_.prepare(scene.primitive_count());

    if (primitive_batch.empty()) {
        scene.set_visible_primitive_indices(primitive_cull_task_.batch_results(), 0);
        return;
    }

    const uint32_t need_cull_count = scene.need_cull_primitive_count();
    const uint32_t worker_thread_count = task_scheduler_.GetNumTaskThreads();
    const size_t max_primitives_per_batch = std::max(
        Scene::kMaxPrimitivesPerCullBatch,
        need_cull_count > 0
            ? static_cast<size_t>(worker_thread_count / need_cull_count)
            : Scene::kMaxPrimitivesPerCullBatch);

    primitive_cull_task_.configure(
        &scene.primitives(),
        &primitive_batch,
        &frustum,
        max_primitives_per_batch,
        need_cull_count);

    task_scheduler_.AddTaskSetToPipe(&primitive_cull_task_);
    task_scheduler_.WaitforTask(&primitive_cull_task_);

    scene.set_visible_primitive_indices(
        primitive_cull_task_.batch_results(),
        primitive_cull_task_.visible_count());
}

void Renderer::cull_scene() {
    if (scene_ == nullptr || camera_ == nullptr) {
        return;
    }

    if (!frustum_culling_enabled_) {
        scene_->make_all_primitives_visible();
        return;
    }

    const math3d::Matrix4 view_projection = camera_->get_projection_matrix() * camera_->get_view_matrix();
    const Frustum frustum(view_projection);

    scene_->cull_clusters(frustum);

    if (!scene_->visible_primitive_indices().empty()) {
        return;
    }

    cull_visible_primitives_parallel(*scene_, frustum);
}

void Renderer::run()
{
    if (async_loader_task_) {
        task_scheduler_.AddTaskSetToPipe(async_loader_task_);

        if (async_wait_fn_) {
            async_wait_fn_();
        }

        task_scheduler_.WaitforTask(async_loader_task_);

        if (async_complete_fn_) {
            async_complete_fn_();
        }

        async_loader_task_ = nullptr;
        async_wait_fn_ = nullptr;
        async_complete_fn_ = nullptr;
    }

    if (scene_ != nullptr && camera_ != nullptr) {
        cull_scene();
    }

    task_scheduler_.AddPinnedTask(&render_task_);
}

}// namespace ocarina
