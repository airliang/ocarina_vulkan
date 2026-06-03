#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Device;
class AsyncLoader : public enki::ITaskSet
{
public:

    // Construct with device and a user-provided lambda executed on a worker thread.
    // The lambda signature should be: void(Device*)
    AsyncLoader(Device* device, ocarina::function<void(Device*)> task)
        : device_(device), task_(std::move(task))
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    // Keep the old single-arg ctor for backward compatibility (no-op task)
    explicit AsyncLoader(Device* device)
        : device_(device)
    {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

private:
    Device* device_;
    ocarina::function<void(Device*)> task_ = nullptr;
};

}// namespace ocarina
