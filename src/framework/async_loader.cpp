#pragma once

#include "async_loader.h"

namespace ocarina {

void AsyncLoader::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)range;
    (void)threadnum;
    if (task_) {
        try {
            task_(device_);
        } catch (...) {
            // Swallow exceptions to avoid propagating them through enki worker threads.
            // If you want logging, add your logging call here.
        }
    }
}

}// namespace ocarina
