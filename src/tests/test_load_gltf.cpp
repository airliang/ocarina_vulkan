//
// Created by Zero on 2022/8/16.
//


#include "core/stl.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "framework/gltf_async_loader.h"
#include "rhi/context.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"

using namespace ocarina;


int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    //FileManager &file_manager = FileManager::instance();
    RHIContext& file_manager = RHIContext::instance();

    auto window = create_sdl_window("display", make_uint2(800, 600));

    InstanceCreation instanceCreation = {};
    //instanceCreation.instanceExtentions =
    instanceCreation.windowHandle = window->get_window_handle();
    Device device = file_manager.create_device("vulkan", instanceCreation);

    enki::TaskScheduler task_scheduler;
    task_scheduler.Initialize();
    
    GltfAsyncLoader test_loading_gltf_task("D:\\Github\\ocarina_vulkan\\res\\FlightHelmet\\glTF\\FlightHelmet.gltf", nullptr);
    task_scheduler.AddPinnedTask(&test_loading_gltf_task);

    task_scheduler.WaitforAllAndShutdown();
}