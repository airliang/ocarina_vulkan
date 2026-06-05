#pragma once

#include "cmd_record_task.h"

namespace ocarina {

void CmdRecordTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)range;
    (void)threadnum;
    if (task_) {
        try {
            task_(device_, cmd_buffer_);
        } catch (...) {
            // Swallow exceptions to avoid propagating them through enki worker threads.
            // If you want logging, add your logging call here.
        }
    }
}

}// namespace ocarina
