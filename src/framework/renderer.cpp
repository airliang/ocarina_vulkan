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
#include "core/logging.h"
#include <chrono>
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
#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t0 = std::chrono::high_resolution_clock::now();
#endif
    scene.build_primitive_cull_batch();
    const std::vector<uint32_t>& primitive_batch = scene.primitive_cull_batch();

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_build_batch = std::chrono::high_resolution_clock::now();
#endif
    primitive_cull_task_.prepare(scene.primitive_count());

    if (primitive_batch.empty()) {
        scene.set_visible_primitive_indices(primitive_cull_task_.batch_results(), 0);
        return;
    }

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_prepare = std::chrono::high_resolution_clock::now();
#endif
    const uint32_t need_cull_count = scene.need_cull_primitive_count();
    const uint32_t worker_thread_count = task_scheduler_.GetNumTaskThreads();
    const size_t max_primitives_per_batch = std::max(
        Scene::kMaxPrimitivesPerCullBatch,
        need_cull_count > 0
            ? static_cast<size_t>(worker_thread_count / need_cull_count)
            : Scene::kMaxPrimitivesPerCullBatch);

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_batch_size = std::chrono::high_resolution_clock::now();
#endif
    primitive_cull_task_.configure(
        &scene.primitives(),
        &primitive_batch,
        &frustum,
        max_primitives_per_batch,
        need_cull_count);

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_configure = std::chrono::high_resolution_clock::now();
#endif
    task_scheduler_.AddTaskSetToPipe(&primitive_cull_task_);
    task_scheduler_.WaitforTask(&primitive_cull_task_);

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_after_wait = std::chrono::high_resolution_clock::now();
#endif
    scene.set_visible_primitive_indices(
        primitive_cull_task_.batch_results(),
        primitive_cull_task_.visible_count());

#if defined(_DEBUG) || !defined(NDEBUG)
    const auto t_end = std::chrono::high_resolution_clock::now();
    auto ms = [](auto a, auto b) -> double {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    const double total_ms = ms(t0, t_end);
    const double build_batch_ms = ms(t0, t_after_build_batch);
    const double prepare_ms = ms(t_after_build_batch, t_after_prepare);
    const double batch_size_ms = ms(t_after_prepare, t_after_batch_size);
    const double configure_ms = ms(t_after_batch_size, t_after_configure);
    const double wait_ms = ms(t_after_configure, t_after_wait);
    const double write_back_ms = ms(t_after_wait, t_end);

    // Perf analysis: WaitForTask typically dominates because it includes the real work:
    // per-primitive AABB transform + frustum test in worker threads.
    OC_INFO_FORMAT(
        "Renderer::cull_visible_primitives_parallel: total={:.3f}ms (buildBatch={:.3f} prepare={:.3f} batchSize={:.3f} configure={:.3f} wait(work)={:.3f} writeBack={:.3f}) "
        "needCull={} visible={} threads={} minRange={}",
        total_ms,
        build_batch_ms,
        prepare_ms,
        batch_size_ms,
        configure_ms,
        wait_ms,
        write_back_ms,
        need_cull_count,
        primitive_cull_task_.visible_count(),
        worker_thread_count,
        static_cast<uint32_t>(max_primitives_per_batch));
#endif
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

    scene_->cull_grids(frustum);
    if (scene_->need_cull_primitive_count() == 0) {
        scene_->set_visible_primitive_indices(primitive_cull_task_.batch_results(), 0);
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
