//
// Compute shader test:
// Pass 1 (ComputePass): dispatch test.compute via RHIRenderPass execute lambda.
// Pass 2 (PostProcess): fullscreen triangle samples that texture to the swapchain.
//

#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "rhi/resources/texture.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/pass_group_id.h"
#include "framework/scene.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/shader_parameters.h"
#include "framework/async_loader.h"
#include "framework/pso_request.h"
#include "framework/frame_resources.h"
#include "rhi/shader_program.h"

using namespace ocarina;

namespace {

RasterState make_fullscreen_raster_state() {
    RasterState raster = RasterState::Default();
    raster.cull_mode = CullingMode::NONE;
    return raster;
}

DepthStencilState make_fullscreen_depth_state() {
    DepthStencilState depth_state = DepthStencilState::Default();
    depth_state.depth_test_enable = false;
    depth_state.depth_write_enable = false;
    return depth_state;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();
    file_manager.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(800, 600);
    auto window = create_sdl_window("Vulkan Compute Shader Test", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Scene scene;
    Renderer renderer(&device);

    ShaderProgram* compute_program = nullptr;
    std::unique_ptr<ShaderParameters> compute_params;
    Material* display_material = nullptr;
    Texture* compute_output = nullptr;
    bool compute_output_initialized = false;

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path compute_path = project_root / "res/shaderlibrary/builtin/test.compute";
    const fs::path fullscreen_vert = project_root / "res/shaderlibrary/builtin/fullscreen.vert";
    const fs::path display_frag = project_root / "res/shaderlibrary/builtin/display_texture.frag";

    const std::string compute_abs = fs::absolute(compute_path).string();
    const std::string fullscreen_vert_abs = fs::absolute(fullscreen_vert).string();
    const std::string display_frag_abs = fs::absolute(display_frag).string();

    const TextureUsageFlags compute_tex_usage = static_cast<TextureUsageFlags>(
        static_cast<uint32_t>(TextureUsageFlags::ShaderReadWrite)
        | static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly));

    compute_output = ResourceManager::instance().create_render_target_texture(
        &device,
        "compute_output",
        window_size.x,
        window_size.y,
        PixelStorage::BYTE4,
        compute_tex_usage);

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        [&, compute_abs, fullscreen_vert_abs, display_frag_abs](Device* load_device) {
            std::set<string> options;
            ResourceManager& resources = ResourceManager::instance();

            compute_program = resources.create_compute_shader_program(
                load_device, compute_abs, options);
            compute_params = std::make_unique<ShaderParameters>(load_device, compute_program);

            ShaderProgram* display_program = resources.create_shader_program(
                load_device, fullscreen_vert_abs, display_frag_abs, options, options);
            display_material = resources.create_material(load_device, display_program);
            display_material->set_raster_state(make_fullscreen_raster_state());
            display_material->set_depth_stencil_state(make_fullscreen_depth_state());
        });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    camera.set_position({0.0f, 0.0f, -2.5f});
    camera.set_target({0.0f, 0.0f, 0.0f});

    // Pass 1: compute-only RHIRenderPass (no render target).
    RenderPassCreation compute_pass_creation;
    compute_pass_creation.render_target = nullptr;
    RHIRenderPass* compute_pass = device.create_render_pass(compute_pass_creation);
    compute_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (compute_program == nullptr || compute_params == nullptr || compute_output == nullptr) {
            return;
        }
        if (!compute_params->is_ready()) {
            return;
        }

        compute_program->ensure_compute_pipeline(&device);
        RHIPipeline* pipeline = compute_program->compute_pipeline();
        if (pipeline == nullptr) {
            return;
        }

        // Set compute parameters before dispatch.
        compute_params->set_texture("outputTexture", compute_output);
        compute_params->apply_uploads();

        const handle_ty tex_handle = reinterpret_cast<handle_ty>(compute_output->impl());
        const TextureLayout src_layout =
            compute_output_initialized ? TextureLayout::ShaderReadOnly : TextureLayout::Undefined;
        cmd.transition_texture_layout(tex_handle, src_layout, TextureLayout::General);
        compute_output_initialized = true;

        // Bind compute pipeline first so descriptor binds use COMPUTE bind point.
        cmd.bind_pipeline(pipeline);
        FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);
        compute_params->bind(cmd, pipeline);
        compute_program->dispatch_for_extent(
            cmd, nullptr, 0, 0, window_size.x, window_size.y);

        cmd.transition_texture_layout(
            tex_handle,
            TextureLayout::General,
            TextureLayout::ShaderReadOnly);
    });

    RenderTarget swapchain_target = RenderTarget::swapchain();

    // Clears swapchain during async loading (only UI+Skybox groups run then).
    RenderPassCreation clear_pass_creation;
    clear_pass_creation.render_target = &swapchain_target;
    clear_pass_creation.clear_color = make_float4(0.05f, 0.05f, 0.08f, 1.0f);
    clear_pass_creation.clear_depth = 1.0f;
    clear_pass_creation.clear_stencil = 0;
    clear_pass_creation.present_swapchain = false;
    RHIRenderPass* clear_pass = device.create_render_pass(clear_pass_creation);

    RenderPassCreation display_pass_creation;
    display_pass_creation.render_target = &swapchain_target;
    display_pass_creation.clear_color = make_float4(0.05f, 0.05f, 0.08f, 1.0f);
    display_pass_creation.clear_depth = 1.0f;
    display_pass_creation.clear_stencil = 0;
    display_pass_creation.present_swapchain = false;
    RHIRenderPass* display_pass = device.create_render_pass(display_pass_creation);
    display_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (display_material == nullptr) {
            return;
        }
        renderer.draw_fullscreen(cmd, display_material, display_pass);
    });

    RenderPassCreation ui_pass_creation;
    ui_pass_creation.render_target = &swapchain_target;
    ui_pass_creation.clear_color_attachment = false;
    ui_pass_creation.clear_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    PSORequest display_pso = PSORequest::make_graphics(
        fullscreen_vert_abs, display_frag_abs, display_pass);
    display_pso.raster_state = make_fullscreen_raster_state();
    display_pso.depth_stencil_state = make_fullscreen_depth_state();
    async_loader.set_pso_requests({std::move(display_pso)});

    renderer.set_scene(&scene);
    renderer.set_camera(&camera);
    renderer.set_frustum_culling_enabled(false);

    renderer.pass_group(PassGroupId::ComputePass).add_render_pass(compute_pass);
    renderer.pass_group(PassGroupId::Skybox).add_render_pass(clear_pass);
    renderer.pass_group(PassGroupId::PostProcess).add_render_pass(display_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Vulkan Compute Shader Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.dt());
    });

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        // PostProcess display_pass clears from here on; drop the loading-only Skybox clearer
        // so we don't wait twice on present_complete in one submit.
        renderer.pass_group(PassGroupId::Skybox).remove_render_pass(clear_pass);

        display_material->set_property(
            "albedo",
            TextureHandle{InvalidUI32, compute_output});
        display_material->add_sampler(
            "sampler_albedo",
            *compute_output->get_sampler_pointer());

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
}
