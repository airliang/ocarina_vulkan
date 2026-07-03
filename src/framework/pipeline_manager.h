#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "pipeline_cache_key.h"
#include "pipeline_create_task.h"
#include "pipeline_layout_cache_key.h"
#include "rhi/pipeline_state.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_set>

namespace enki { class TaskScheduler; }

namespace ocarina {

class Device;
class RHIRenderPass;
struct RHIPipeline;
struct RHIPipelineLayout;

class PipelineManager {
public:
    static PipelineManager& instance() noexcept;

    void initialize(Device* device, enki::TaskScheduler* scheduler);
    void shutdown() noexcept;

    void enqueue(const PipelineState& pipeline_state, RHIRenderPass* render_pass);
    [[nodiscard]] RHIPipeline* get_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;
    [[nodiscard]] bool has_pipeline(const PipelineState& pipeline_state, RHIRenderPass* render_pass) const noexcept;
    [[nodiscard]] RHIPipelineLayout* get_or_create_pipeline_layout(
        const handle_ty shaders[PipelineState::MAX_SHADER_STAGE]);

    void process_create_queue();
    void on_create_task_finished() noexcept;

private:
    struct PipelineRequest {
        PipelineCacheKey key;
    };

    friend class PipelineCreateTask;

    [[nodiscard]] bool pop_next_request(PipelineRequest& out_request);
    void process_request(const PipelineRequest& request);
    void try_dispatch_create_task();
    void clear_cache() noexcept;

    Device* device_ = nullptr;
    enki::TaskScheduler* scheduler_ = nullptr;
    PipelineCreateTask create_task_;

    mutable std::mutex cache_mutex_;
    std::unordered_map<PipelineCacheKey, RHIPipeline*, PipelineCacheKeyHash> pipelines_;
    std::unordered_map<PipelineLayoutCacheKey, RHIPipelineLayout*, PipelineLayoutCacheKeyHash> pipeline_layouts_;

    std::mutex queue_mutex_;
    std::deque<PipelineRequest> request_queue_;
    std::unordered_set<PipelineCacheKey, PipelineCacheKeyHash> pending_keys_;

    std::mutex dispatch_mutex_;
    std::atomic<bool> create_task_active_{false};
    std::atomic<bool> shutdown_requested_{false};
    bool initialized_ = false;
};

}// namespace ocarina
