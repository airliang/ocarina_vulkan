#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "pipeline_compile_task.h"
#include "pipeline_layout_cache_key.h"
#include "pso_request.h"
#include "rhi/pipeline_cache_key.h"
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

    /// Optional root for builtin shaders (res/shaderlibrary/builtin). Auto-detected if empty.
    void set_shader_library_root(fs::path root) noexcept { shader_library_root_ = std::move(root); }

    /// Enqueue the default mesh PSO for @p render_pass (typically the swapchain pass).
    void create_default_psos(RHIRenderPass* render_pass);

    /// Enqueue a PSO compile request. Ignores duplicates already in the request queue.
    /// Returns the submitted task (or nullptr if ignored / already cached).
    PipelineCompileTask* enqueue(
        PSORequest request,
        LoadingProgressListener* progress_listener = nullptr);

    /// Runtime path: material already has resolved shader handles.
    PipelineCompileTask* enqueue(
        const PipelineState& pipeline_state,
        RHIRenderPass* render_pass);

    void update();

    [[nodiscard]] RHIPipeline* get_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;
    [[nodiscard]] bool has_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;
    [[nodiscard]] bool has_pipeline(const PipelineCacheKey& key) const noexcept;

    RHIPipelineLayout* create_and_cache_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]);

    [[nodiscard]] RHIPipelineLayout* get_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) const noexcept;

    [[nodiscard]] RHIPipelineLayout* get_or_create_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]) const noexcept {
        return get_pipeline_layout(shaders);
    }

    void insert_pipeline_cache(const PipelineCacheKey& key, RHIPipeline* pipeline) noexcept;

    void on_compile_task_finished(const PSORequest& request, const PipelineCacheKey& key) noexcept;

    [[nodiscard]] PipelineCompileTaskPool& task_pool() noexcept { return task_pool_; }
    [[nodiscard]] Device* device() const noexcept { return device_; }
    [[nodiscard]] enki::TaskScheduler* scheduler() const noexcept { return scheduler_; }

private:
    friend class PipelineCompileTask;

    void clear_cache() noexcept;
    [[nodiscard]] fs::path resolve_builtin_shader_dir() const;
    [[nodiscard]] bool try_mark_pending_request(const PSORequest& request) noexcept;
    void clear_pending_request(const PSORequest& request) noexcept;
    [[nodiscard]] bool try_mark_pending_key(const PipelineCacheKey& key) noexcept;

    Device* device_ = nullptr;
    enki::TaskScheduler* scheduler_ = nullptr;
    PipelineCompileTaskPool task_pool_;
    fs::path shader_library_root_;
    bool default_requests_enqueued_ = false;

    mutable std::mutex cache_mutex_;
    std::unordered_map<PipelineCacheKey, RHIPipeline*, PipelineCacheKeyHash> pipelines_;
    std::unordered_map<PipelineLayoutCacheKey, RHIPipelineLayout*, PipelineLayoutCacheKeyHash> pipeline_layouts_;

    std::mutex pending_mutex_;
    std::unordered_set<PipelineCacheKey, PipelineCacheKeyHash> pending_keys_;
    std::unordered_set<PSORequest, PSORequestHash, PSORequestIdentityEqual> pending_requests_;

    std::atomic<bool> shutdown_requested_{false};
    bool initialized_ = false;
};

}// namespace ocarina
