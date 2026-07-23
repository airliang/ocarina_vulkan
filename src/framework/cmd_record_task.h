#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Device;
class CommandBuffer;

class CmdRecordTask : public enki::ITaskSet {
public:
    using RecordFn = ocarina::function<void(Device*, CommandBuffer*)>;

    CmdRecordTask() noexcept {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    // Construct with device and a user-provided lambda executed on a worker thread.
    // The lambda signature should be: void(Device*, CommandBuffer*)
    CmdRecordTask(Device* device, CommandBuffer& cmd_buffer, RecordFn task)
        : device_(device), cmd_buffer_(&cmd_buffer), task_(std::move(task)) {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    // Keep the old ctor for backward compatibility (no-op task)
    explicit CmdRecordTask(Device* device, CommandBuffer& cmd_buffer)
        : device_(device), cmd_buffer_(&cmd_buffer) {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void configure(Device* device, CommandBuffer& cmd_buffer, RecordFn task) noexcept {
        device_ = device;
        cmd_buffer_ = &cmd_buffer;
        task_ = std::move(task);
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

private:
    Device* device_ = nullptr;
    RecordFn task_ = nullptr;
    CommandBuffer* cmd_buffer_ = nullptr;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina
