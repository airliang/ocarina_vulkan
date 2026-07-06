#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "shader_compile_task.h"

namespace ocarina {

class Device;
class LoadingProgressListener;

/// Worker-thread loader: optionally compiles shaders in parallel, then runs load().
class AsyncLoader : public enki::ITaskSet {
public:
    AsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        std::vector<ShaderCompileTask::Entry>* shader_entries,
        LoadingProgressListener* progress_listener = nullptr)
        : scheduler_(scheduler),
          device_(device),
          shader_entries_(shader_entries),
          progress_listener_(progress_listener)
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    AsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        std::vector<ShaderCompileTask::Entry>* shader_entries,
        ocarina::function<void(Device*)> task,
        LoadingProgressListener* progress_listener = nullptr)
        : scheduler_(scheduler),
          device_(device),
          shader_entries_(shader_entries),
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

    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

protected:
    virtual void load(Device* device) { (void)device; }

    /// Extra loading steps after shader compilation (e.g. glTF scene items).
    [[nodiscard]] virtual uint32_t count_load_progress_steps() { return 0; }

    void run_shader_compile_task() noexcept;

    enki::TaskScheduler* scheduler_ = nullptr;
    Device* device_ = nullptr;
    std::vector<ShaderCompileTask::Entry>* shader_entries_ = nullptr;
    LoadingProgressListener* progress_listener_ = nullptr;

private:
    ocarina::function<void(Device*)> task_ = nullptr;
    ocarina::function<void()> complete_callback_ = nullptr;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina
