#include "pipeline_manager.h"

#include "frame_resources.h"
#include "loading_progress_listener.h"
#include "resource_manager.h"
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

bool PipelineManager::try_mark_pending_request(const PSORequest& request) noexcept {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (pending_requests_.find(request) != pending_requests_.end()) {
        return false;
    }
    pending_requests_.insert(request);
    return true;
}

void PipelineManager::clear_pending_request(const PSORequest& request) noexcept {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_.erase(request);
}

bool PipelineManager::try_mark_pending_key(const PipelineCacheKey& key) noexcept {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (pending_keys_.find(key) != pending_keys_.end()) {
        return false;
    }
    pending_keys_.insert(key);
    return true;
}

void PipelineManager::on_compile_task_finished(
    const PSORequest& request,
    const PipelineCacheKey& key) noexcept {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(request);
        if (key.render_pass != nullptr) {
            pending_keys_.erase(key);
        }
    }
}

void PipelineManager::insert_pipeline_cache(const PipelineCacheKey& key, RHIPipeline* pipeline) noexcept {
    if (pipeline == nullptr || key.render_pass == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    pipelines_.emplace(key, pipeline);
}

PipelineCompileTask* PipelineManager::enqueue(
    PSORequest request,
    LoadingProgressListener* progress_listener) {
    if (!initialized_ || scheduler_ == nullptr || device_ == nullptr
        || request.render_pass == nullptr
        || shutdown_requested_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    if (!try_mark_pending_request(request)) {
        return nullptr;
    }

    // Resolve shader modules from cache when paths are provided.
    if (request.has_shader_paths() && !request.has_shader_handles()) {
        ShaderProgram* program = ResourceManager::instance().get_shader_program(
            request.vertex_shader_path,
            request.pixel_shader_path,
            request.vertex_options,
            request.pixel_options);
        if (program != nullptr) {
            program->ensure_gpu_shaders(device_);
            request.vertex_shader = program->shader_handle(ShaderType::VertexShader);
            request.pixel_shader = program->shader_handle(ShaderType::PixelShader);
        }
    }

    if (request.has_shader_handles()) {
        const PipelineCacheKey key = request.make_cache_key();
        if (has_pipeline(key)) {
            clear_pending_request(request);
            return nullptr;
        }
        if (!try_mark_pending_key(key)) {
            clear_pending_request(request);
            return nullptr;
        }
    }

    PipelineCompileTask* task = task_pool_.Acquire();
    task->Initialize(device_, this, &task_pool_, std::move(request), progress_listener);
    scheduler_->AddTaskSetToPipe(task);
    return task;
}

PipelineCompileTask* PipelineManager::enqueue(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass) {
    return enqueue(PSORequest::from_pipeline_state(pipeline_state, render_pass), nullptr);
}

void PipelineManager::update() {
    if (!initialized_) {
        return;
    }
    task_pool_.reclaim();
}

RHIPipeline* PipelineManager::get_pipeline(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass) const noexcept {
    if (render_pass == nullptr) {
        return nullptr;
    }

    const PipelineCacheKey key = MakePipelineCacheKey(pipeline_state, render_pass);
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    const auto it = pipelines_.find(key);
    return it != pipelines_.end() ? it->second : nullptr;
}

bool PipelineManager::has_pipeline(const PipelineCacheKey& key) const noexcept {
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    return pipelines_.find(key) != pipelines_.end();
}

bool PipelineManager::has_pipeline(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass) const noexcept {
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

    RHIPipelineLayout* cached_layout = nullptr;
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        const PipelineLayoutCacheKey key{shaders[0], shaders[1]};
        const auto [it, inserted] = pipeline_layouts_.emplace(key, pipeline_layout);
        if (!inserted) {
            device_->destroy_pipeline_layout(pipeline_layout);
            cached_layout = it->second;
        } else {
            cached_layout = pipeline_layout;
        }
    }

    FrameResources::instance().ensure_global_descriptor_sets(cached_layout);
    return cached_layout;
}

void PipelineManager::clear_cache() noexcept {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_keys_.clear();
        pending_requests_.clear();
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
