#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "pipeline_compile_task.h"

namespace ocarina {

class Device;
class LoadingProgressListener;
class RHIRenderPass;

/// Worker-thread loader: compiles pipelines (shaders → layouts → PSO), then runs load().
class AsyncLoader : public enki::ITaskSet {
public:
    AsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        std::vector<PipelineCompileTask::Entry>* pipeline_entries,
        RHIRenderPass* target_render_pass = nullptr,
        LoadingProgressListener* progress_listener = nullptr)
        : scheduler_(scheduler),
          device_(device),
          pipeline_entries_(pipeline_entries),
          target_render_pass_(target_render_pass),
          progress_listener_(progress_listener)
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    AsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        std::vector<PipelineCompileTask::Entry>* pipeline_entries,
        ocarina::function<void(Device*)> task,
        RHIRenderPass* target_render_pass = nullptr,
        LoadingProgressListener* progress_listener = nullptr)
        : scheduler_(scheduler),
          device_(device),
          pipeline_entries_(pipeline_entries),
          target_render_pass_(target_render_pass),
          progress_listener_(progress_listener),
          task_(std::move(task))
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    explicit AsyncLoader(Device* device, ocarina::function<void(Device*)> task)
        : device_(device), task_(std::move(task))
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    explicit AsyncLoader(Device* device)
        : device_(device)
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

    void set_progress_listener(LoadingProgressListener* progress_listener) noexcept
    {
        progress_listener_ = progress_listener;
    }

    void set_complete_callback(ocarina::function<void()> complete_callback) noexcept
    {
        complete_callback_ = std::move(complete_callback);
    }

    // One compile task per target (Entry + render pass). Preferred over target_render_pass_.
    void set_compile_targets(std::vector<PipelineCompileTarget> targets) noexcept
    {
        compile_targets_ = std::move(targets);
    }

    void set_target_render_pass(RHIRenderPass* render_pass) noexcept
    {
        target_render_pass_ = render_pass;
    }

    [[nodiscard]] RHIRenderPass* target_render_pass() const noexcept
    {
        return target_render_pass_;
    }

    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

protected:
    virtual void load(Device* device) { (void)device; }

    /// Extra loading steps after pipeline compilation (e.g. glTF scene items).
    [[nodiscard]] virtual uint32_t count_load_progress_steps() { return 0; }

    void run_pipeline_compile_tasks() noexcept;
    [[nodiscard]] std::vector<PipelineCompileTarget> build_compile_targets() const noexcept;
    [[nodiscard]] uint32_t count_pending_shader_steps() const noexcept;

    enki::TaskScheduler* scheduler_ = nullptr;
    Device* device_ = nullptr;
    std::vector<PipelineCompileTask::Entry>* pipeline_entries_ = nullptr;
    std::vector<PipelineCompileTarget> compile_targets_;
    RHIRenderPass* target_render_pass_ = nullptr;
    LoadingProgressListener* progress_listener_ = nullptr;

private:
    ocarina::function<void(Device*)> task_ = nullptr;
    ocarina::function<void()> complete_callback_ = nullptr;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina
