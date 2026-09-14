#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "rhi/resources/cubemap.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/scene.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/pso_request.h"
#include "framework/frame_resources.h"
#include "framework/pass_group_id.h"
#include "core/image.h"

using namespace ocarina;

namespace {

RasterState make_skybox_raster_state() {
    RasterState raster = RasterState::Default();
    raster.cull_mode = CullingMode::NONE;
    return raster;
}

DepthStencilState make_skybox_depth_state() {
    DepthStencilState depth_state = DepthStencilState::Default();
    depth_state.depth_test_enable = true;
    depth_state.depth_write_enable = false;
    depth_state.depth_compare_op = SamplerCompareFunc::LE;
    return depth_state;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();
    file_manager.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Skybox", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Scene scene;
    Renderer renderer(&device);

    Material* skybox_material = nullptr;
    Cubemap* skybox_cubemap = nullptr;

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path shader_vert = project_root / "res/shaderlibrary/builtin/fullscreen.vert";
    const fs::path shader_frag = project_root / "res/shaderlibrary/builtin/skybox.frag";
    const fs::path cubemap_dir = project_root / "res/textures/Yokohama2";

    const std::string shader_vert_abs = fs::absolute(shader_vert).string();
    const std::string shader_frag_abs = fs::absolute(shader_frag).string();

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        [&skybox_material, &skybox_cubemap, shader_vert_abs, shader_frag_abs, cubemap_dir](Device* device) {
            std::set<string> options;
            ResourceManager& resources = ResourceManager::instance();
            ShaderProgram* program = resources.create_shader_program(
                device, shader_vert_abs, shader_frag_abs, options, options);
            skybox_material = resources.create_material(device, program);

            // Must match the PSORequest used for async compile (see set_pso_requests below).
            skybox_material->set_depth_stencil_state(make_skybox_depth_state());
            skybox_material->set_raster_state(make_skybox_raster_state());

            const fs::path face_paths[6] = {
                cubemap_dir / "posx.jpg",
                cubemap_dir / "negx.jpg",
                cubemap_dir / "posy.jpg",
                cubemap_dir / "negy.jpg",
                cubemap_dir / "posz.jpg",
                cubemap_dir / "negz.jpg",
            };

            Image faces[6];
            for (uint32_t i = 0; i < 6; ++i) {
                faces[i] = Image::load(face_paths[i], ColorSpace::SRGB);
            }

            TextureSampler sampler{
                TextureSampler::Filter::LINEAR_LINEAR,
                TextureSampler::Address::EDGE};
            skybox_cubemap = resources.create_cubemap(device, faces, sampler);

            skybox_material->set_cubemap("skybox", skybox_cubemap);
            skybox_material->add_sampler(
                "sampler_skybox",
                sampler);
        });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    camera.set_position({0.0f, 0.0f, 0.0f});
    camera.set_target({0.0f, 0.0f, 1.0f});

    RenderTarget swapchain_target = RenderTarget::swapchain();

    RenderPassCreation skybox_pass_creation;
    skybox_pass_creation.render_target = &swapchain_target;
    skybox_pass_creation.clear_color = make_float4(0.02f, 0.02f, 0.02f, 1.0f);
    skybox_pass_creation.clear_depth = 1.0f;
    skybox_pass_creation.clear_stencil = 0;
    skybox_pass_creation.present_swapchain = false;
    RHIRenderPass* skybox_pass = device.create_render_pass(skybox_pass_creation);

    RenderPassCreation ui_pass_creation;
    ui_pass_creation.render_target = &swapchain_target;
    ui_pass_creation.clear_color_attachment = false;
    ui_pass_creation.clear_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    // make_graphics() defaults to cull BACK + default depth; skybox needs NONE + LE/no-write.
    PSORequest skybox_pso = PSORequest::make_graphics(shader_vert_abs, shader_frag_abs, skybox_pass);
    skybox_pso.raster_state = make_skybox_raster_state();
    skybox_pso.depth_stencil_state = make_skybox_depth_state();
    async_loader.set_pso_requests({std::move(skybox_pso)});

    renderer.set_scene(&scene);
    renderer.set_camera(&camera);
    renderer.set_frustum_culling_enabled(false);
    renderer.pass_group(PassGroupId::Skybox).add_render_pass(skybox_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Vulkan Skybox Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.dt());
    });

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        renderer.set_skybox_material(skybox_material);
        imgui_renderer.set_frame_callback([&]() {
            display_frame_info(*window->widgets());
        });
    });

    auto image_io = Image::pure_color(make_float4(0.1f, 0.1f, 0.15f, 1), ColorSpace::LINEAR, make_uint2(500));
    window->set_background(image_io.pixel_ptr<float4>(), make_uint2(800, 600));

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
}
