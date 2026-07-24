//
// Created by Zero on 2022/8/16.
//

#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <windows.h>
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/primitive.h"
#include "framework/scene.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"
#include "framework/camera.h"
#include "rhi/imgui_creation.h"
#include "framework/mesh.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/transform.h"
#include "framework/async_loader.h"
#include "framework/pipeline_compile_task.h"
#include "framework/frame_resources.h"

using namespace ocarina;

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();

    const uint2 window_size = make_uint2(800, 600);
    auto window = create_sdl_window("display", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Scene scene;
    Primitive& quad = scene.emplace_primitive();
    Material* material = nullptr;
    Mesh* quad_mesh = nullptr;
    Texture* texture = nullptr;

    Renderer renderer(&device);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path shader_vert = project_root / "res/shaderlibrary/builtin/texture.vert";
    const fs::path shader_frag = project_root / "res/shaderlibrary/builtin/texture.frag";
    const fs::path texture_path = project_root / "res/textures/granite.png";

    std::vector<PipelineCompileTask::Entry> pipeline_entries;
    pipeline_entries.push_back(PipelineCompileTask::Entry::make_graphics(
        fs::absolute(shader_vert).string(),
        fs::absolute(shader_frag).string()));

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        &pipeline_entries,
        [&material, &quad_mesh, &texture, &pipeline_entries, &texture_path](Device* device) {
        material = ResourceManager::instance().create_material(
            device,
            pipeline_entries[0].vertex_shader(),
            pipeline_entries[0].pixel_shader());

        Image image = Image::load(texture_path, ColorSpace::SRGB);
        TextureViewCreation texture_view = {};
        texture_view.mip_level_count = 0;
        texture_view.usage = TextureUsageFlags::ShaderReadOnly;
        TextureSampler sampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};
        texture = ResourceManager::instance().create_texture(device, image, texture_view, sampler);

        quad_mesh = ResourceManager::instance().create_mesh("quad");
    });

    auto setup_quad = [&](Primitive& quad) {
        quad.set_mesh(quad_mesh);
        quad.set_material(material);

        material->add_texture(hash64("albedo"), texture);
        material->add_sampler(hash64("sampler_albedo"), *texture->get_sampler_pointer());
    };

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(800.0f / 600.0f);
    camera.set_position({0.0f, 0.0f, -2.5f});
    camera.set_target({0.0f, 0.0f, 0.0f});

    uint64_t model_matrix_name_id = hash64("modelMatrix");
    uint64_t model_matrix_inverse_name_id = hash64("modelMatrixInverse");
    auto update_push_constant = [&](Primitive& primitive, TransformComponent& transform) {
        const float4x4 world_matrix = transform.get_world_matrix();
        const float4x4 world_matrix_inverse = inverse(world_matrix);
        primitive.set_push_constant_variable(
            model_matrix_name_id,
            reinterpret_cast<std::byte*>(const_cast<float4x4*>(&world_matrix)),
            sizeof(world_matrix));
        primitive.set_push_constant_variable(
            model_matrix_inverse_name_id,
            reinterpret_cast<std::byte*>(const_cast<float4x4*>(&world_matrix_inverse)),
            sizeof(world_matrix_inverse));
    };

    quad.set_update_push_constant_function(update_push_constant);

    RenderPassCreation render_pass_creation;
    render_pass_creation.swapchain_clear_color = make_float4(0.1f, 0.1f, 0.1f, 1.0f);
    render_pass_creation.swapchain_clear_depth = 1.0f;
    render_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass* render_pass = device.create_render_pass(render_pass_creation);

    renderer.set_scene(&scene);
    renderer.set_camera(&camera);
    renderer.pass_group(PassGroupId::UI).add_render_pass(render_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Vulkan Texture Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.loading_dt());
    });

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        quad.set_geometry_data_setup(&device, [&](Primitive& quad) {
            setup_quad(quad);
        });

        imgui_renderer.set_frame_callback([&]() {
            display_frame_info(*window->widgets());
        });
    });

    auto image_io = Image::pure_color(make_float4(1, 0, 0, 1), ColorSpace::LINEAR, make_uint2(500));
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
