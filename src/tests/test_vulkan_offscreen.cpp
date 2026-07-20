//
// Offscreen render target test using Vulkan dynamic rendering (vkCmdBeginRendering).
// Pass 1: render a colored triangle to an offscreen texture (white clear) via ECS entity draw call.
// Pass 2: blit result to swapchain via a textured fullscreen quad from the scene.
//

#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <windows.h>
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/pass_group_id.h"
#include "framework/primitive.h"
#include "framework/scene.h"
#include "framework/entity_component_system.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"
#include "framework/camera.h"
#include "framework/mesh.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/pipeline_compile_task.h"
#include "framework/frame_resources.h"
#include "framework/global_gpu_storage.h"
#include "framework/transform_component.h"

using namespace ocarina;

namespace {

struct GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix;
    math3d::Matrix4 view_matrix;
};

Mesh* create_triangle_mesh() {
    Mesh* mesh = ocarina::new_with_allocator<Mesh>();
    Vector3 positions[3] = {
        {-1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
    };
    Vector4 colors[3] = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    const std::vector<uint16_t> indices{0, 1, 2};

    MeshGeometryInput input{};
    input.vertex_count = 3;
    input.positions = positions;
    input.colors = colors;
    input.indices = indices.data();
    input.index_count = static_cast<uint32_t>(indices.size());
    mesh->set_geometry_slice(GlobalGPUStorage::instance().append_mesh(input));
    return mesh;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();

    const uint2 window_size = make_uint2(800, 600);
    auto window = create_sdl_window("Vulkan Offscreen Test", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    const uint32_t triangle_entity_index = ecs.emplace_primitive();

    Scene scene;
    Primitive& quad = scene.emplace_primitive();

    Material* triangle_material = nullptr;
    Material* quad_material = nullptr;
    Mesh* triangle_mesh = nullptr;
    Mesh* quad_mesh = nullptr;

    Renderer renderer(&device);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path triangle_vert = project_root / "res/shaderlibrary/builtin/triangle.vert";
    const fs::path triangle_frag = project_root / "res/shaderlibrary/builtin/triangle.frag";
    const fs::path texture_vert = project_root / "res/shaderlibrary/builtin/texture.vert";
    const fs::path texture_frag = project_root / "res/shaderlibrary/builtin/texture.frag";

    std::vector<PipelineCompileTask::Entry> pipeline_entries;
    pipeline_entries.push_back(PipelineCompileTask::Entry::make_graphics(
        fs::absolute(triangle_vert).string(),
        fs::absolute(triangle_frag).string()));
    pipeline_entries.push_back(PipelineCompileTask::Entry::make_graphics(
        fs::absolute(texture_vert).string(),
        fs::absolute(texture_frag).string()));

    const TextureUsageFlags offscreen_usage = static_cast<TextureUsageFlags>(
        static_cast<uint32_t>(TextureUsageFlags::RenderTarget) | static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly));

    Texture* offscreen_color = ResourceManager::instance().create_render_target_texture(
        &device,
        "offscreen_color",
        window_size.x,
        window_size.y,
        PixelStorage::BYTE4,
        offscreen_usage);

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        &pipeline_entries,
        [&](Device* load_device) {
        triangle_material = ResourceManager::instance().create_material(
            load_device,
            pipeline_entries[0].vertex_shader(),
            pipeline_entries[0].pixel_shader());
        quad_material = ResourceManager::instance().create_material(
            load_device,
            pipeline_entries[1].vertex_shader(),
            pipeline_entries[1].pixel_shader());
        quad_mesh = ResourceManager::instance().create_mesh("quad");
    });

    auto setup_offscreen_triangle = [&](Primitive& primitive) {
        primitive.set_mesh(triangle_mesh);
        primitive.set_material(triangle_material);
    };

    auto setup_quad = [&](Primitive& primitive) {
        primitive.set_mesh(quad_mesh);
        primitive.set_material(quad_material);
        quad_material->add_texture(hash64("albedo"), offscreen_color);
        quad_material->add_sampler(hash64("sampler_albedo"), *offscreen_color->get_sampler_pointer());
    };

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(800.0f / 600.0f);
    camera.set_position({0.0f, 0.0f, -2.5f});
    camera.set_target({0.0f, 0.0f, 0.0f});

    const uint64_t model_matrix_name_id = hash64("modelMatrix");
    const uint64_t model_matrix_inverse_name_id = hash64("modelMatrixInverse");

    RenderPassCreation offscreen_pass_creation;
    offscreen_pass_creation.color_attachment_count = 1;
    offscreen_pass_creation.color_attachments[0] = offscreen_color;
    offscreen_pass_creation.clear_color = make_float4(1.0f, 1.0f, 1.0f, 1.0f);
    RHIRenderPass* offscreen_pass = device.create_render_pass(offscreen_pass_creation);

    RenderPassCreation swapchain_pass_creation;
    swapchain_pass_creation.swapchain_clear_color = make_float4(0.1f, 0.1f, 0.1f, 1.0f);
    swapchain_pass_creation.swapchain_clear_depth = 1.0f;
    swapchain_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass* swapchain_pass = device.create_render_pass(swapchain_pass_creation);

    renderer.set_scene(&scene);
    renderer.set_camera(&camera);
    renderer.set_render_pass_primitive_filter([&](uint32_t entity_index, RHIRenderPass* render_pass) {
        (void)entity_index;
        return render_pass == swapchain_pass;
    });

    // Pass groups: Offscreen RT first, then UI/swapchain (opaque present + ImGui).
    renderer.pass_group(PassGroupId::Offscreen).add_render_pass(offscreen_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(swapchain_pass);

    async_loader.set_compile_targets({
        PipelineCompileTarget{&pipeline_entries[0], offscreen_pass},
        PipelineCompileTarget{&pipeline_entries[1], swapchain_pass},
    });

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Vulkan Offscreen Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.loading_dt());
    });

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        triangle_mesh = create_triangle_mesh();
        ResourceManager::instance().add_mesh("offscreen_triangle", triangle_mesh);

        Primitive& triangle = ecs.primitive(triangle_entity_index);

        triangle.set_update_push_constant_function([&](Primitive& primitive, TransformComponent& transform) {
            primitive.set_push_constant_variable(
                model_matrix_name_id,
                reinterpret_cast<std::byte*>(const_cast<void*>(static_cast<const void*>(&transform.get_world_matrix()))),
                sizeof(transform.get_world_matrix()));
        });

        quad.set_update_push_constant_function([&](Primitive& primitive, TransformComponent& transform) {
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
        });

        triangle.set_geometry_data_setup(&device, [&](Primitive& primitive) {
            setup_offscreen_triangle(primitive);
        });

        quad.set_geometry_data_setup(&device, [&](Primitive& primitive) {
            setup_quad(primitive);
        });

        triangle.update_render_component(
            &device,
            ecs.render_component(triangle_entity_index),
            ecs.transform_component(triangle_entity_index));
        offscreen_pass->add_draw_call(
            triangle_entity_index,
            triangle_material->get_pipeline_state());

        imgui_renderer.set_frame_callback([&]() {
            display_frame_info(*window->widgets());
        });
    });

    FrameResources::instance().set_update_callback([&](FrameResources&, double dt) {
        (void)dt;
        if (triangle_material == nullptr) {
            return;
        }

        Primitive& triangle = ecs.primitive(triangle_entity_index);
        triangle.update_render_component(
            &device,
            ecs.render_component(triangle_entity_index),
            ecs.transform_component(triangle_entity_index));

        DescriptorSet* global_descriptor_set = FrameResources::instance().get_global_descriptor_set("global_ubo");
        if (global_descriptor_set == nullptr) {
            return;
        }
        GlobalUniformBuffer global_ubo_data = {
            camera.get_projection_matrix().transpose(),
            camera.get_view_matrix().transpose()};
        global_descriptor_set->update_buffer(hash64("global_ubo"), &global_ubo_data, sizeof(GlobalUniformBuffer));
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
