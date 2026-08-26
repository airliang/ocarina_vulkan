#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "pso_request.h"
#include "rhi/pipeline_cache_key.h"
#include "rhi/pipeline_state.h"

#include <deque>
#include <mutex>
#include <vector>

namespace ocarina {

class Device;
class LoadingProgressListener;
class PipelineManager;
class PipelineCompileTaskPool;
class RHIRenderPass;

/// Compiles shaders (cache miss), builds layouts / PSO, inserts into PipelineManager.
class PipelineCompileTask : public enki::ITaskSet {
public:
    PipelineCompileTask() noexcept {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void Initialize(
        Device* device,
        PipelineManager* manager,
        PipelineCompileTaskPool* pool,
        PSORequest request,
        LoadingProgressListener* progress_listener = nullptr) noexcept;

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

    [[nodiscard]] const PSORequest& request() const noexcept { return request_; }
    [[nodiscard]] const PipelineCacheKey& cache_key() const noexcept { return cache_key_; }

private:
    void reset() noexcept;
    void resolve_shaders() noexcept;
    void create_layouts_and_pipeline() noexcept;
    void finish_and_release() noexcept;

    Device* device_ = nullptr;
    PipelineManager* manager_ = nullptr;
    PipelineCompileTaskPool* pool_ = nullptr;
    LoadingProgressListener* progress_listener_ = nullptr;
    PSORequest request_{};
    PipelineCacheKey cache_key_{};
};

class PipelineCompileTaskPool {
public:
    PipelineCompileTaskPool() = default;
    ~PipelineCompileTaskPool();

    PipelineCompileTaskPool(const PipelineCompileTaskPool&) = delete;
    PipelineCompileTaskPool& operator=(const PipelineCompileTaskPool&) = delete;

    [[nodiscard]] PipelineCompileTask* Acquire();
    void Release(PipelineCompileTask* task);
    void reclaim() noexcept;
    void clear() noexcept;

private:
    std::mutex mutex_;
    std::vector<PipelineCompileTask*> owned_;
    std::deque<PipelineCompileTask*> free_;
    std::deque<PipelineCompileTask*> pending_release_;
};

}// namespace ocarina
