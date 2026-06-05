#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Device;
class CommandBuffer;
class CmdRecordTask : public enki::ITaskSet
{
public:

    // Construct with device and a user-provided lambda executed on a worker thread.
    // The lambda signature should be: void(Device*, CommandBuffer*)
    CmdRecordTask(Device* device, CommandBuffer& cmd_buffer, ocarina::function<void(Device*, CommandBuffer*)> task)
        : device_(device), cmd_buffer_(&cmd_buffer), task_(std::move(task))
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    // Keep the old single-arg ctor for backward compatibility (no-op task)
    explicit CmdRecordTask(Device* device, CommandBuffer& cmd_buffer)
        : device_(device), cmd_buffer_(&cmd_buffer)
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

private:
    Device* device_;
    ocarina::function<void(Device*, CommandBuffer*)> task_ = nullptr;
    CommandBuffer* cmd_buffer_;
};

}// namespace ocarina
