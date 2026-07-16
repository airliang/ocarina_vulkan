#include "pipeline_manager.h"

#include "loading_progress_listener.h"
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
    worker_thread_rr_.store(0, std::memory_order_relaxed);
    initialized_ = device_ != nullptr && scheduler_ != nullptr;
}

void PipelineManager::shutdown() noexcept {
    if (!initialized_) {
        return;
    }

    shutdown_requested_.store(true, std::memory_order_release);
    clear_cache();
    task_pool_.clear();
    initialized_ = false;
    device_ = nullptr;
    scheduler_ = nullptr;
}

bool PipelineManager::try_mark_pending(const PipelineCacheKey& key) noexcept {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (pending_keys_.find(key) != pending_keys_.end()) {
        return false;
    }
    pending_keys_.insert(key);
    return true;
}

void PipelineManager::on_compile_task_finished(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass) noexcept {
    if (render_pass == nullptr) {
        return;
    }
    const PipelineCacheKey key = MakePipelineCacheKey(pipeline_state, render_pass);
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_keys_.erase(key);
}

void PipelineManager::insert_pipeline_cache(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass,
    RHIPipeline* pipeline) noexcept {
    if (pipeline == nullptr || render_pass == nullptr) {
        return;
    }
    const PipelineCacheKey key = MakePipelineCacheKey(pipeline_state, render_pass);
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    pipelines_.emplace(key, pipeline);
}

void PipelineManager::enqueue(const PipelineState& pipeline_state, RHIRenderPass* render_pass) {
    if (!initialized_ || render_pass == nullptr || scheduler_ == nullptr
        || shutdown_requested_.load(std::memory_order_acquire)) {
        return;
    }

    const PipelineCacheKey key = MakePipelineCacheKey(pipeline_state, render_pass);

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        if (pipelines_.find(key) != pipelines_.end()) {
            return;
        }
    }

    if (!try_mark_pending(key)) {
        return;
    }

    PipelineCompileTask* task = task_pool_.Acquire();
    task->Initialize(
        device_,
        this,
        &task_pool_,
        pipeline_state,
        render_pass,
        next_worker_thread_num());
    scheduler_->AddPinnedTask(task);
}

void PipelineManager::update() {
    if (!initialized_) {
        return;
    }
    task_pool_.reclaim();
}

void PipelineManager::submit_compile_target(
    const PipelineCompileTarget& target,
    LoadingProgressListener* progress_listener) {
    if (target.entry == nullptr || target.render_pass == nullptr) {
        return;
    }

    if (!target.entry->is_graphics()) {
        PipelineCompileTask* task = task_pool_.Acquire();
        task->Initialize(
            device_,
            this,
            &task_pool_,
            target.entry,
            target.render_pass,
            next_worker_thread_num(),
            progress_listener);
        scheduler_->AddPinnedTask(task);
        return;
    }

    const PipelineState pipeline_state = PipelineCompileTask::MakePipelineStateFromEntry(target.entry);
    if (has_pipeline(pipeline_state, target.render_pass)) {
        return;
    }

    const PipelineCacheKey key{pipeline_state, target.render_pass};
    if (!try_mark_pending(key)) {
        return;
    }

    PipelineCompileTask* task = task_pool_.Acquire();
    task->Initialize(
        device_,
        this,
        &task_pool_,
        target.entry,
        target.render_pass,
        next_worker_thread_num(),
        progress_listener);
    scheduler_->AddPinnedTask(task);
}

void PipelineManager::compile_targets(
    const std::vector<PipelineCompileTarget>& targets,
    LoadingProgressListener* progress_listener) {
    if (!initialized_ || scheduler_ == nullptr || device_ == nullptr
        || targets.empty() || shutdown_requested_.load(std::memory_order_acquire)) {
        return;
    }

    std::unordered_set<PipelineCompileTask::Entry*> resolved_entries;
    for (const PipelineCompileTarget& target : targets) {
        if (target.entry == nullptr) {
            continue;
        }
        if (!resolved_entries.insert(target.entry).second) {
            continue;
        }

        PipelineCompileTask::ResolveEntryShaders(device_, target.entry, progress_listener);
        if (target.entry->is_graphics()) {
            const PipelineState pipeline_state =
                PipelineCompileTask::MakePipelineStateFromEntry(target.entry);
            create_and_cache_pipeline_layout(pipeline_state.shaders);
        }
    }

    for (const PipelineCompileTarget& target : targets) {
        submit_compile_target(target, progress_listener);
    }
}

uint32_t PipelineManager::next_worker_thread_num() noexcept {
    if (scheduler_ == nullptr) {
        return 1;
    }

    const uint32_t num_threads = scheduler_->GetNumTaskThreads();
    if (num_threads <= 1) {
        return 0;
    }
    if (num_threads == 2) {
        return 1;
    }

    const uint32_t worker_count = num_threads - 2;
    const uint32_t index = worker_thread_rr_.fetch_add(1, std::memory_order_relaxed) % worker_count;
    return 2 + index;
}

RHIPipeline* PipelineManager::get_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept {
    if (render_pass == nullptr) {
        return nullptr;
    }

    const PipelineCacheKey key = MakePipelineCacheKey(pipeline_state, render_pass);
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    const auto it = pipelines_.find(key);
    return it != pipelines_.end() ? it->second : nullptr;
}

bool PipelineManager::has_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept {
    return get_pipeline(pipeline_state, render_pass) != nullptr;
}

RHIPipelineLayout* PipelineManager::get_pipeline_layout(
    const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) const noexcept {
    if (shaders[0] == InvalidUI64 || shaders[1] == InvalidUI64) {
        return nullptr;
    }

    const PipelineLayoutCacheKey key{shaders[0], shaders[1]};
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    const auto it = pipeline_layouts_.find(key);
    return it != pipeline_layouts_.end() ? it->second : nullptr;
}

RHIPipelineLayout* PipelineManager::create_and_cache_pipeline_layout(
    const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) {
    if (device_ == nullptr || shaders[0] == InvalidUI64 || shaders[1] == InvalidUI64) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        const PipelineLayoutCacheKey key{shaders[0], shaders[1]};
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
    const PipelineLayoutCacheKey key{shaders[0], shaders[1]};
    const auto [it, inserted] = pipeline_layouts_.emplace(key, pipeline_layout);
    if (!inserted) {
        device_->destroy_pipeline_layout(pipeline_layout);
        return it->second;
    }
    return pipeline_layout;
}

void PipelineManager::clear_cache() noexcept {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_keys_.clear();
    }

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
}

}// namespace ocarina
