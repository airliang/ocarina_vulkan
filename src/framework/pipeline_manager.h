#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "pipeline_cache_key.h"
#include "pipeline_compile_task.h"
#include "pipeline_layout_cache_key.h"
#include "rhi/pipeline_state.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace enki { class TaskScheduler; }

namespace ocarina {

class Device;
class LoadingProgressListener;
class RHIRenderPass;
struct RHIPipeline;
struct RHIPipelineLayout;

class PipelineManager {
public:
    static PipelineManager& instance() noexcept;

    void initialize(Device* device, enki::TaskScheduler* scheduler);
    void shutdown() noexcept;

    // Runtime: acquire a pooled PipelineCompileTask and AddPinnedTask immediately.
    void enqueue(const PipelineState& pipeline_state, RHIRenderPass* render_pass);

    // Reclaim finished pooled tasks (safe to call every frame from the render thread).
    void update();

    [[nodiscard]] RHIPipeline* get_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;
    [[nodiscard]] bool has_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;

    // Creates descriptor-set layouts + pipeline layout and caches them.
    RHIPipelineLayout* create_and_cache_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]);

    [[nodiscard]] RHIPipelineLayout* get_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) const noexcept;

    [[nodiscard]] RHIPipelineLayout* get_or_create_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) const noexcept {
        return get_pipeline_layout(shaders);
    }

    // Async-load: resolve shaders synchronously, then submit one async task per target.
    void compile_targets(
        const std::vector<PipelineCompileTarget>& targets,
        LoadingProgressListener* progress_listener = nullptr);

    void insert_pipeline_cache(
        const PipelineState& pipeline_state,
        RHIRenderPass* render_pass,
        RHIPipeline* pipeline) noexcept;

    void on_compile_task_finished(
        const PipelineState& pipeline_state,
        RHIRenderPass* render_pass) noexcept;

    [[nodiscard]] PipelineCompileTaskPool& task_pool() noexcept { return task_pool_; }
    [[nodiscard]] uint32_t next_worker_thread_num() noexcept;

    [[nodiscard]] Device* device() const noexcept { return device_; }
    [[nodiscard]] enki::TaskScheduler* scheduler() const noexcept { return scheduler_; }

private:
    friend class PipelineCompileTask;

    void submit_compile_target(
        const PipelineCompileTarget& target,
        LoadingProgressListener* progress_listener = nullptr);
    void clear_cache() noexcept;
    [[nodiscard]] bool try_mark_pending(const PipelineCacheKey& key) noexcept;

    Device* device_ = nullptr;
    enki::TaskScheduler* scheduler_ = nullptr;
    PipelineCompileTaskPool task_pool_;

    mutable std::mutex cache_mutex_;
    std::unordered_map<PipelineCacheKey, RHIPipeline*, PipelineCacheKeyHash> pipelines_;
    std::unordered_map<PipelineLayoutCacheKey, RHIPipelineLayout*, PipelineLayoutCacheKeyHash> pipeline_layouts_;

    std::mutex pending_mutex_;
    std::unordered_set<PipelineCacheKey, PipelineCacheKeyHash> pending_keys_;

    std::atomic<uint32_t> worker_thread_rr_{0};
    std::atomic<bool> shutdown_requested_{false};
    bool initialized_ = false;
};

}// namespace ocarina
