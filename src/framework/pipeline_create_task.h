#pragma once

#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class PipelineManager;

class PipelineCreateTask : public enki::ITaskSet {
public:
    PipelineCreateTask() {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void configure(PipelineManager* manager) noexcept;

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

private:
    PipelineManager* manager_ = nullptr;
};

}// namespace ocarina
