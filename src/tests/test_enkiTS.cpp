//
// Created by Zero on 2022/8/16.
//


#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"

using namespace ocarina;

class TestTaskSet : public enki::ITaskSet
{
public:
    int32_t data[1000] = { 0 };
    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
    {
        printf("Hello from thread %u, range %u to %u\n", threadnum, range.start, range.end);
        for (uint32_t i = range.start; i < range.end; ++i)
        {
            data[i]++;
        }
    }
};

class TestPinnedTask : public enki::IPinnedTask
{
public:
    void Execute() override
    {
        printf("Hello from pinned task on thread %u\n", threadNum);
    }
};


int main(int argc, char *argv[]) {
    enki::TaskScheduler task_scheduler;
    task_scheduler.Initialize();
    TestTaskSet test_task_set;
    test_task_set.m_SetSize = 1000;
    test_task_set.m_MinRange = 100;
    TestPinnedTask test_pinned_task;
    task_scheduler.AddTaskSetToPipe(&test_task_set);
    task_scheduler.AddPinnedTask(&test_pinned_task);

    task_scheduler.WaitforAllAndShutdown();
}