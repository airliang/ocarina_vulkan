#include "pipeline_create_task.h"
#include "pipeline_manager.h"

namespace ocarina {

void PipelineCreateTask::configure(PipelineManager* manager) noexcept {
    manager_ = manager;
}

void PipelineCreateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)range;
    (void)threadnum;

    if (manager_ == nullptr) {
        return;
    }

    manager_->process_create_queue();
}

}// namespace ocarina
