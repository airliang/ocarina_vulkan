//
// AlphaBlendModeTest:
// - Pipeline: Opaque + Transparent only (no DepthPrepass).
// - Loads res/AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf
// - glTF MASK → ALPHA_MASK=1; BLEND → alpha blend + ALPHA_BLEND=1
//

#include "core/stl.h"
#include "core/logging.h"
#include "math/basic_types.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/render_component.h"
#include "framework/camera.h"
#include "framework/gltf_async_loader.h"
#include "framework/loading_progress_listener.h"
#include "framework/scene.h"
#include "framework/pass_group_id.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"

using namespace ocarina;

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();
    context.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Alpha Blend Mode Test", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path repo_root = source_dir.parent_path().parent_path();
    const fs::path gltf_path =
        repo_root / "res/AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf";
    const fs::path pipeline_path =
        repo_root / "res/pipelines/forward_opaque_transparent.pipeline.json";
    const fs::path shader_vert = repo_root / "res/shaderlibrary/builtin/mesh.vert";
    const fs::path shader_frag = repo_root / "res/shaderlibrary/builtin/mesh.frag";

    LoadingProgressListener loading_progress;

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(
        static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    // Frame the AlphaBlendModeTest bed / sample boxes.
    camera.set_position({0.0f, 0.8f, 2.2f});
    camera.set_target({0.0f, 0.4f, 0.0f});
    camera.set_move_speed(1.5f);

    Renderer renderer(&device);
    renderer.set_camera(&camera);
    renderer.set_loading_progress_listener(&loading_progress);

    RenderTarget swapchain_target = RenderTarget::swapchain();
    if (!renderer.load_default_pipeline(&swapchain_target, pipeline_path)) {
        OC_ERROR("Failed to load ForwardOpaqueTransparent pipeline");
        return -1;
    }

    RHIRenderPass* opaque_pass = renderer.find_pipeline_render_pass("Opaque");
    RHIRenderPass* transparent_pass = renderer.find_pipeline_render_pass("Transparent");
    if (opaque_pass == nullptr || transparent_pass == nullptr) {
        OC_ERROR("Pipeline missing Opaque / Transparent RHIRenderPass");
        return -1;
    }

    RenderPassCreation ui_pass_creation;
    ui_pass_creation.render_target = &swapchain_target;
    ui_pass_creation.clear_color_attachment = false;
    ui_pass_creation.clear_depth_attachment = false;
    // ImGui does not need depth; avoid LOAD on an uninitialized swapchain depth image
    // while the loader is still running (no DepthPrepass in this pipeline).
    ui_pass_creation.enable_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    const ocarina_string shader_vert_abs =
        ocarina::to_ocarina_string(fs::absolute(shader_vert).string());
    const ocarina_string shader_frag_abs =
        ocarina::to_ocarina_string(fs::absolute(shader_frag).string());
    GltfAsyncLoader gltf_loader(
        &renderer.task_scheduler(),
        &device,
        &renderer,
        shader_vert_abs,
        shader_frag_abs,
        ocarina::to_ocarina_string(fs::absolute(gltf_path).string()));
    gltf_loader.set_progress_listener(&loading_progress);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Alpha Blend Mode Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), &loading_progress, renderer.dt());
    });

    renderer.set_render_pass_primitive_filter(
        [opaque_pass, transparent_pass](uint32_t, RHIRenderPass* pass) {
            return pass == opaque_pass || pass == transparent_pass;
        });
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    renderer.set_async_loader(&gltf_loader, nullptr, [&]() {
        Scene& scene = gltf_loader.get_scene();
        for (uint32_t index = 0; index < scene.primitive_count(); ++index) {
            scene.render_component(index).set_update_push_constant_function(
                [](RenderComponent& render, uint32_t entity_index) {
                    render.set_push_constant_variable("transform_index", entity_index);
                });
        }
        renderer.set_scene(&scene);

        imgui_renderer.set_frame_callback([&]() {
            display_frame_info(*window->widgets());
        });
    });

    renderer.set_loading_gui_impl_callback([&](const CommandBuffer& cmd_buffer) {
        imgui_renderer.render(cmd_buffer);
    });
    renderer.set_render_gui_impl_callback([&](const CommandBuffer& cmd_buffer) {
        imgui_renderer.render(cmd_buffer);
    });
    renderer.set_render_task_end_callback([&]() {
        imgui_renderer.cleanup();
        window->remove_event_listener(&camera);
    });

    renderer.run();
    window->run([](double) {});
    renderer.shutdown();
    return 0;
}
