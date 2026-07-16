#include "async_loader.h"
#include "enki_task_debug.h"
#include "loading_progress_listener.h"
#include "pipeline_manager.h"
#include "rhi/device.h"

namespace ocarina {

std::vector<PipelineCompileTarget> AsyncLoader::build_compile_targets() const noexcept {
    if (!compile_targets_.empty()) {
        return compile_targets_;
    }

    std::vector<PipelineCompileTarget> targets;
    if (pipeline_entries_ == nullptr || target_render_pass_ == nullptr) {
        return targets;
    }

    targets.reserve(pipeline_entries_->size());
    for (PipelineCompileTask::Entry& entry : *pipeline_entries_) {
        targets.push_back(PipelineCompileTarget{&entry, target_render_pass_});
    }
    return targets;
}

uint32_t AsyncLoader::count_pending_shader_steps() const noexcept {
    const std::vector<PipelineCompileTarget> targets = build_compile_targets();
    uint32_t shader_count = 0;
    for (const PipelineCompileTarget& target : targets) {
        if (target.entry != nullptr) {
            shader_count += target.entry->pending_shader_count();
        }
    }
    return shader_count;
}

void AsyncLoader::run_pipeline_compile_tasks() noexcept {
    if (scheduler_ == nullptr || device_ == nullptr) {
        return;
    }

    const std::vector<PipelineCompileTarget> targets = build_compile_targets();
    if (targets.empty()) {
        return;
    }

    const uint32_t shader_count = count_pending_shader_steps();
    const uint32_t load_steps = count_load_progress_steps();
    const uint32_t total_steps = shader_count + load_steps;
    if (progress_listener_ != nullptr && total_steps > 0) {
        progress_listener_->begin("Loading", total_steps);
        progress_listener_->set_phase("Compiling pipelines");
    }

    PipelineManager::instance().compile_targets(targets, progress_listener_);
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
