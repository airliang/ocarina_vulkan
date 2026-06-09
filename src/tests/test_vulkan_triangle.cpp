//
// Created by Zero on 2022/8/16.
//

#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <windows.h>
#include "math/base.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/buffer.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/renderer.h"
#include "framework/primitive.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"
#include "framework/camera.h"
#include "rhi/imgui_creation.h"
#include "framework/mesh.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/frame_resources.h"

using namespace ocarina;

struct GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix;
    math3d::Matrix4 view_matrix;
};

static Mesh* create_triangle_mesh(Device* device) {
    Mesh* mesh = ocarina::new_with_allocator<Mesh>();

    VertexBuffer* vertex_buffer = device->create_vertex_buffer();
    Vector3 positions[3] = {
        {-1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
    };
    vertex_buffer->add_vertex_stream(
        VertexAttributeType::Enum::Position,
        3,
        sizeof(Vector3),
        static_cast<const void*>(positions));

    Vector4 colors[3] = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    vertex_buffer->add_vertex_stream(
        VertexAttributeType::Enum::Color0,
        3,
        sizeof(Vector4),
        static_cast<const void*>(colors));
    vertex_buffer->upload_data();

    const std::vector<uint16_t> indices{0, 1, 2};
    IndexBuffer* index_buffer = device->create_index_buffer(indices.data(), static_cast<uint32_t>(indices.size()));

    mesh->set_vertex_buffer(vertex_buffer);
    mesh->set_index_buffer(index_buffer);
    return mesh;
}

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();

    auto window = create_sdl_window("display", make_uint2(800, 600));

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Primitive triangle;
    Material* material = nullptr;
    Mesh* triangle_mesh = nullptr;

    AsyncLoader async_loader(&device, [&material, &triangle_mesh](Device* device) {
        std::set<string> options;
        handle_ty vertex_shader = device->create_shader_from_file(
            "D:\\github\\Vision\\src\\ocarina\\src\\backends\\vulkan\\builtin\\triangle.vert",
            ShaderType::VertexShader,
            options);
        handle_ty pixel_shader = device->create_shader_from_file(
            "D:\\github\\Vision\\src\\ocarina\\src\\backends\\vulkan\\builtin\\triangle.frag",
            ShaderType::PixelShader,
            options);

        material = ResourceManager::instance().create_material(device, vertex_shader, pixel_shader);
        triangle_mesh = create_triangle_mesh(device);
        ResourceManager::instance().add_mesh("triangle", triangle_mesh);
    });

    auto setup_triangle = [&](Primitive& triangle) {
        triangle.set_mesh(triangle_mesh);
        triangle.set_material(material);
    };

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(800.0f / 600.0f);
    camera.set_position({0.0f, 0.0f, -2.5f});
    camera.set_target({0.0f, 0.0f, 0.0f});

    auto pre_render_draw_item = [&](const DrawCallItem& item) {
    };

    uint64_t model_matrix_name_id = hash64("modelMatrix");
    auto update_push_constant = [&](Primitive& primitive) {
        primitive.set_push_constant_variable(
            model_matrix_name_id,
            reinterpret_cast<std::byte*>(const_cast<void*>(static_cast<const void*>(&primitive.get_world_matrix()))),
            sizeof(primitive.get_world_matrix()));
    };

    triangle.set_draw_call_pre_render_function(pre_render_draw_item);
    triangle.set_update_push_constant_function(update_push_constant);

    RenderPassCreation render_pass_creation;
    render_pass_creation.swapchain_clear_color = make_float4(0.1f, 0.1f, 0.1f, 1.0f);
    render_pass_creation.swapchain_clear_depth = 1.0f;
    render_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass* render_pass = device.create_render_pass(render_pass_creation);

    Renderer renderer(&device);
    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        triangle.set_geometry_data_setup(&device, [&](Primitive& triangle) {
            setup_triangle(triangle);
            if (material != nullptr) {
                material->set_target_render_pass(render_pass);
            }
        });
    });

    FrameResources::instance().set_update_callback([&](FrameResources&, double dt) {
        camera.update(dt);
        DescriptorSet* global_descriptor_set = FrameResources::instance().get_global_descriptor_set("global_ubo");
        GlobalUniformBuffer global_ubo_data = {
            camera.get_projection_matrix().transpose(),
            camera.get_view_matrix().transpose()};
        global_descriptor_set->update_buffer(hash64("global_ubo"), &global_ubo_data, sizeof(GlobalUniformBuffer));
        render_pass->clear_draw_call_items();
        auto draw_item = triangle.get_draw_call_item(&device, render_pass);
        render_pass->add_draw_call(draw_item);
    });

    renderer.add_render_pass(render_pass);

    auto image_io = Image::pure_color(make_float4(1, 0, 0, 1), ColorSpace::LINEAR, make_uint2(500));
    window->set_background(image_io.pixel_ptr<float4>(), make_uint2(800, 600));
    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    string window_name = "Vulkan Triangle Test";
    imgui_renderer.set_frame_callback([&]() {
        window->widgets()->push_window(window_name);
        window->widgets()->text("FPS: %.2f", 1.0f / renderer.dt());
        window->widgets()->pop_window();
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
