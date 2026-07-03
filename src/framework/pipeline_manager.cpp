#include "pipeline_manager.h"

#include "rhi/device.h"
#include "rhi/renderpass.h"

namespace ocarina {

PipelineManager& PipelineManager::instance() noexcept {
    static PipelineManager manager;
    return manager;
}

void PipelineManager::initialize(Device* device, enki::TaskScheduler* scheduler) {
    device_ = device;
    scheduler_ = scheduler;
    shutdown_requested_.store(false, std::memory_order_release);
    create_task_active_.store(false, std::memory_order_release);
    initialized_ = device_ != nullptr && scheduler_ != nullptr;
}

void PipelineManager::shutdown() noexcept {
    if (!initialized_) {
        return;
    }

    shutdown_requested_.store(true, std::memory_order_release);

    if (scheduler_ != nullptr && create_task_active_.load(std::memory_order_acquire)) {
        scheduler_->WaitforTask(&create_task_);
    }

    clear_cache();
    initialized_ = false;
    device_ = nullptr;
    scheduler_ = nullptr;
    create_task_active_.store(false, std::memory_order_release);
}

void PipelineManager::enqueue(const PipelineState& pipeline_state, RHIRenderPass* render_pass) {
    if (!initialized_ || render_pass == nullptr || shutdown_requested_.load(std::memory_order_acquire)) {
        return;
    }

    const PipelineCacheKey key{pipeline_state, render_pass};

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        if (pipelines_.find(key) != pipelines_.end()) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        if (pending_keys_.find(key) != pending_keys_.end()) {
            return;
        }
        pending_keys_.insert(key);
        request_queue_.push_back(PipelineRequest{key});
    }

    try_dispatch_create_task();
}

void PipelineManager::try_dispatch_create_task() {
    if (!initialized_ || scheduler_ == nullptr || shutdown_requested_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> dispatch_lock(dispatch_mutex_);

    if (create_task_active_.load(std::memory_order_acquire)) {
        return;
    }

    bool has_work = false;
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        has_work = !request_queue_.empty();
    }

    if (!has_work) {
        return;
    }

    create_task_active_.store(true, std::memory_order_release);
    create_task_.configure(this);
    scheduler_->AddTaskSetToPipe(&create_task_);
}

RHIPipeline* PipelineManager::get_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept {
    if (render_pass == nullptr) {
        return nullptr;
    }

    const PipelineCacheKey key{pipeline_state, render_pass};
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    const auto it = pipelines_.find(key);
    return it != pipelines_.end() ? it->second : nullptr;
}

bool PipelineManager::has_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept {
    return get_pipeline(pipeline_state, render_pass) != nullptr;
}

void PipelineManager::process_create_queue() {
    PipelineRequest request;
    while (pop_next_request(request)) {
        process_request(request);
    }
    on_create_task_finished();
}

bool PipelineManager::pop_next_request(PipelineRequest& out_request) {
    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
    if (request_queue_.empty()) {
        return false;
    }

    out_request = std::move(request_queue_.front());
    request_queue_.pop_front();
    return true;
}

void PipelineManager::on_create_task_finished() noexcept {
    create_task_active_.store(false, std::memory_order_release);

    if (shutdown_requested_.load(std::memory_order_acquire)) {
        return;
    }

    try_dispatch_create_task();
}

void PipelineManager::process_request(const PipelineRequest& request) {
    if (device_ == nullptr) {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        pending_keys_.erase(request.key);
        return;
    }

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        if (pipelines_.find(request.key) != pipelines_.end()) {
            std::lock_guard<std::mutex> queue_lock(queue_mutex_);
            pending_keys_.erase(request.key);
            return;
        }
    }

    RHIPipelineLayout* pipeline_layout = get_or_create_pipeline_layout(request.key.pipeline_state.shaders);
    if (pipeline_layout == nullptr) {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        pending_keys_.erase(request.key);
        return;
    }

    RHIPipeline* pipeline = device_->create_pipeline(
        request.key.pipeline_state,
        request.key.render_pass,
        pipeline_layout);
    if (pipeline != nullptr) {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        pipelines_.emplace(request.key, pipeline);
    }

    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        pending_keys_.erase(request.key);
    }
}

RHIPipelineLayout* PipelineManager::get_or_create_pipeline_layout(
    const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) {
    if (device_ == nullptr || shaders[0] == InvalidUI64 || shaders[1] == InvalidUI64) {
        return nullptr;
    }

    const PipelineLayoutCacheKey key{shaders[0], shaders[1]};

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        const auto it = pipeline_layouts_.find(key);
        if (it != pipeline_layouts_.end()) {
            return it->second;
        }
    }

    PipelineLayoutDesc desc{};
    if (!device_->build_pipeline_layout_desc(shaders, desc)) {
        return nullptr;
    }

    RHIPipelineLayout* pipeline_layout = device_->create_pipeline_layout(desc);
    if (pipeline_layout == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    const auto [it, inserted] = pipeline_layouts_.emplace(key, pipeline_layout);
    if (!inserted) {
        device_->destroy_pipeline_layout(pipeline_layout);
        return it->second;
    }
    return pipeline_layout;
}

void PipelineManager::clear_cache() noexcept {
    if (device_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    for (auto& entry : pipelines_) {
        device_->destroy_pipeline(entry.second);
    }
    pipelines_.clear();

    for (auto& entry : pipeline_layouts_) {
        device_->destroy_pipeline_layout(entry.second);
    }
    pipeline_layouts_.clear();

    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
    request_queue_.clear();
    pending_keys_.clear();
}

}// namespace ocarina
