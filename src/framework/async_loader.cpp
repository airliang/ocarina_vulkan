#include "async_loader.h"
#include "enki_task_debug.h"
#include "loading_progress_listener.h"
#include "pipeline_manager.h"
#include "rhi/device.h"

namespace ocarina {

uint32_t AsyncLoader::count_pending_shader_steps() const noexcept {
    uint32_t shader_count = 0;
    for (const PSORequest& request : pso_requests_) {
        if (!request.has_shader_handles()) {
            if (!request.vertex_shader_path.empty()) {
                ++shader_count;
            }
            if (!request.pixel_shader_path.empty()) {
                ++shader_count;
            }
        }
    }
    return shader_count;
}

void AsyncLoader::run_pipeline_compile_tasks() noexcept {
    if (scheduler_ == nullptr || device_ == nullptr || pso_requests_.empty()) {
        return;
    }

    const uint32_t shader_count = count_pending_shader_steps();
    const uint32_t load_steps = count_load_progress_steps();
    const uint32_t total_steps = shader_count + load_steps;
    if (progress_listener_ != nullptr && total_steps > 0) {
        progress_listener_->begin("Loading", total_steps);
        progress_listener_->set_phase("Compiling pipelines");
    }

    std::vector<PipelineCompileTask*> submitted;
    submitted.reserve(pso_requests_.size());
    for (PSORequest& request : pso_requests_) {
        if (request.render_pass == nullptr) {
            request.render_pass = target_render_pass_;
        }
        if (PipelineCompileTask* task =
                PipelineManager::instance().enqueue(request, progress_listener_)) {
            submitted.push_back(task);
        }
    }

    for (PipelineCompileTask* task : submitted) {
        scheduler_->WaitforTask(task);
    }
}

void AsyncLoader::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)range;
    (void)threadnum;

    run_pipeline_compile_tasks();

    if (task_) {
        try {
            task_(device_);
        } catch (...) {
        }
    } else {
        load(device_);
    }

    if (complete_callback_) {
        try {
            complete_callback_();
        } catch (...) {
        }
        complete_callback_ = nullptr;
    }
}

}// namespace ocarina
