#include "pipeline_compile_task.h"

#include "loading_progress_listener.h"
#include "pipeline_manager.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

void PipelineCompileTask::reset() noexcept {
    device_ = nullptr;
    manager_ = nullptr;
    pool_ = nullptr;
    progress_listener_ = nullptr;
    request_ = {};
    cache_key_ = {};
    m_SetSize = 1;
    m_MinRange = 1;
}

void PipelineCompileTask::Initialize(
    Device* device,
    PipelineManager* manager,
    PipelineCompileTaskPool* pool,
    PSORequest request,
    LoadingProgressListener* progress_listener) noexcept {
    reset();
    device_ = device;
    manager_ = manager;
    pool_ = pool;
    request_ = std::move(request);
    progress_listener_ = progress_listener;
}

void PipelineCompileTask::resolve_shaders() noexcept {
    if (device_ == nullptr) {
        return;
    }

    if (request_.has_shader_handles()) {
        return;
    }

    if (!request_.has_shader_paths()) {
        return;
    }

    if (request_.vertex_shader == 0 || request_.vertex_shader == InvalidUI64) {
        request_.vertex_shader = device_->create_shader_from_file(
            request_.vertex_shader_path,
            ShaderType::VertexShader,
            request_.vertex_options);
        if (progress_listener_ != nullptr) {
            progress_listener_->advance();
        }
    }

    if (request_.pixel_shader == 0 || request_.pixel_shader == InvalidUI64) {
        request_.pixel_shader = device_->create_shader_from_file(
            request_.pixel_shader_path,
            ShaderType::PixelShader,
            request_.pixel_options);
        if (progress_listener_ != nullptr) {
            progress_listener_->advance();
        }
    }
}

void PipelineCompileTask::create_layouts_and_pipeline() noexcept {
    if (manager_ == nullptr || device_ == nullptr || request_.render_pass == nullptr) {
        return;
    }
    if (!request_.has_shader_handles()) {
        return;
    }

    cache_key_ = request_.make_cache_key();
    if (manager_->has_pipeline(cache_key_)) {
        return;
    }

    const PipelineState pipeline_state = request_.make_pipeline_state();
    RHIPipelineLayout* pipeline_layout =
        manager_->create_and_cache_pipeline_layout(pipeline_state.shaders);
    if (pipeline_layout == nullptr) {
        return;
    }

    RHIPipeline* pipeline = device_->create_pipeline(
        pipeline_state,
        request_.render_pass,
        pipeline_layout);
    if (pipeline != nullptr) {
        manager_->insert_pipeline_cache(cache_key_, pipeline);
    }
}

void PipelineCompileTask::finish_and_release() noexcept {
    if (manager_ != nullptr) {
        manager_->on_compile_task_finished(request_, cache_key_);
    }
    if (pool_ != nullptr) {
        pool_->Release(this);
    }
}

void PipelineCompileTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)threadnum;
    if (range.start != 0) {
        return;
    }

    resolve_shaders();
    create_layouts_and_pipeline();
    finish_and_release();
}

PipelineCompileTaskPool::~PipelineCompileTaskPool() {
    clear();
}

PipelineCompileTask* PipelineCompileTaskPool::Acquire() {
    reclaim();

    std::lock_guard<std::mutex> lock(mutex_);
    while (!free_.empty()) {
        PipelineCompileTask* task = free_.front();
        free_.pop_front();
        if (task != nullptr && task->GetIsComplete()) {
            return task;
        }
        if (task != nullptr) {
            pending_release_.push_back(task);
        }
    }

    auto* task = ocarina::new_with_allocator<PipelineCompileTask>();
    owned_.push_back(task);
    return task;
}

void PipelineCompileTaskPool::Release(PipelineCompileTask* task) {
    if (task == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pending_release_.push_back(task);
}

void PipelineCompileTaskPool::reclaim() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<PipelineCompileTask*> still_running;
    while (!pending_release_.empty()) {
        PipelineCompileTask* task = pending_release_.front();
        pending_release_.pop_front();
        if (task == nullptr) {
            continue;
        }
        if (!task->GetIsComplete()) {
            still_running.push_back(task);
            continue;
        }
        free_.push_back(task);
    }
    for (PipelineCompileTask* task : still_running) {
        pending_release_.push_back(task);
    }
}

void PipelineCompileTaskPool::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    free_.clear();
    pending_release_.clear();
    for (PipelineCompileTask* task : owned_) {
        ocarina::delete_with_allocator(task);
    }
    owned_.clear();
}

}// namespace ocarina
