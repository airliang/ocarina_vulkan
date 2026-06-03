#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Device;
class AsyncLoader : public enki::IPinnedTask
{
public:

    // Construct with device and a user-provided lambda that will be executed in Execute().
    // The lambda signature should be: void(Device*)
    AsyncLoader(Device* device, ocarina::function<void(Device*)> task)
    {
        device_ = device;
        task_ = std::move(task);
    }

    // Keep the old single-arg ctor for backward compatibility (no-op task)
    AsyncLoader(Device* device)
    {
        device_ = device;
    }

    void Execute() override;

private:
    Device* device_;
    ocarina::function<void(Device*)> task_ = nullptr;
};

}// namespace ocarina