#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/renderer.h"
#include "framework/primitive.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/frame_resources.h"
#include "framework/gltf_async_loader.h"
#include "framework/scene.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"

using namespace ocarina;

struct GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix;
    math3d::Matrix4 view_matrix;
    float4 camera_pos;
    float4 light_pos;
};

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("FlightHelmet glTF", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path src_root = source_dir.parent_path();
    const fs::path repo_root = src_root.parent_path();
    const fs::path gltf_path = repo_root / "res/FlightHelmet/glTF/FlightHelmet.gltf";
    const fs::path shader_vert = src_root / "backends/vulkan/builtin/mesh.vert";
    const fs::path shader_frag = src_root / "backends/vulkan/builtin/mesh.frag";

    Material* material = nullptr;
    GltfAsyncLoader* gltf_loader = nullptr;

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(1280.0f / 720.0f);
    camera.set_position({0.0f, 0.05f, -0.45f});
    camera.set_target({0.0f, 0.05f, 0.0f});

    Renderer renderer(&device);
    renderer.set_camera(&camera);

    AsyncLoader async_loader(&device, [&](Device* load_device) {
        std::set<string> options;
        handle_ty vertex_shader = load_device->create_shader_from_file(
            fs::absolute(shader_vert).string(),
            ShaderType::VertexShader,
            options);
        handle_ty pixel_shader = load_device->create_shader_from_file(
            fs::absolute(shader_frag).string(),
            ShaderType::PixelShader,
            options);

        material = ResourceManager::instance().create_material(load_device, vertex_shader, pixel_shader);
        gltf_loader = ocarina::new_with_allocator<GltfAsyncLoader>(
            fs::absolute(gltf_path).string(),
            load_device,
            material);
        gltf_loader->Execute();
    });

    const uint64_t model_matrix_name_id = hash64("modelMatrix");
    const uint64_t albedo_index_name_id = hash64("albedoIndex");
    const uint64_t albedo_sampler_index_name_id = hash64("albedoSamplerIndex");
    const uint64_t albedo_texture_name_id = hash64("albedo");
    constexpr uint32_t kAlbedoSamplerIndex = 0; // linear wrap
    auto update_push_constant = [&](Primitive& primitive, TransformComponent& transform) {
        primitive.set_push_constant_variable(
            model_matrix_name_id,
            reinterpret_cast<std::byte*>(const_cast<void*>(static_cast<const void*>(&transform.get_world_matrix()))),
            sizeof(transform.get_world_matrix()));

        const Primitive::TextureHandle albedo_handle = primitive.get_texture_handle(albedo_texture_name_id);
        primitive.set_push_constant_variable(
            albedo_index_name_id,
            reinterpret_cast<std::byte*>(const_cast<uint32_t*>(&albedo_handle.bindless_index_)),
            sizeof(albedo_handle.bindless_index_));
        primitive.set_push_constant_variable(
            albedo_sampler_index_name_id,
            reinterpret_cast<std::byte*>(const_cast<uint32_t*>(&kAlbedoSamplerIndex)),
            sizeof(kAlbedoSamplerIndex));
    };

    RenderPassCreation render_pass_creation;
    render_pass_creation.swapchain_clear_color = make_float4(0.15f, 0.15f, 0.18f, 1.0f);
    render_pass_creation.swapchain_clear_depth = 1.0f;
    render_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass* render_pass = device.create_render_pass(render_pass_creation);

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        if (gltf_loader == nullptr) {
            return;
        }

        Scene& scene = gltf_loader->get_scene();
        for (uint32_t index = 0; index < scene.primitive_count(); ++index) {
            Primitive& primitive = scene.primitive(index);
            primitive.set_update_push_constant_function(update_push_constant);
            primitive.set_geometry_data_setup(&device, [&](Primitive& prim) {
                if (material != nullptr) {
                    material->set_target_render_pass(render_pass);
                }
                (void)prim;
            });
        }
        renderer.set_scene(&scene);
    });

    FrameResources::instance().set_update_callback([&](FrameResources&, double dt) {
        DescriptorSet* global_descriptor_set = FrameResources::instance().get_global_descriptor_set("global_ubo");
        const math3d::Vector3D& cam_position = camera.get_position();
        GlobalUniformBuffer global_ubo_data = {
            camera.get_projection_matrix().transpose(),
            camera.get_view_matrix().transpose(),
            make_float4(cam_position[0], cam_position[1], cam_position[2], 1.0f),
            make_float4(5.0f, 10.0f, 5.0f, 1.0f)};
        global_descriptor_set->update_buffer(hash64("global_ubo"), &global_ubo_data, sizeof(GlobalUniformBuffer));

        render_pass->clear_draw_call_items();
        if (gltf_loader != nullptr) {
            Scene& scene = gltf_loader->get_scene();
            renderer.ensure_render_components(scene.primitive_count());
            for (uint32_t primitive_index : scene.visible_primitive_indices()) {
                Primitive& primitive = scene.primitive(primitive_index);
                primitive.update_render_component(
                    &device,
                    renderer.ecs().render_component(primitive_index),
                    scene.transform_component(primitive_index));
                RenderComponent& render_component = renderer.ecs().render_component(primitive_index);
                render_pass->add_draw_call(primitive_index, render_component.pipeline);
            }
        }
    });

    renderer.add_render_pass(render_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "FlightHelmet glTF";
    imgui_renderer.set_frame_callback([&]() {
        window->widgets()->push_window(window_name);
        window->widgets()->text("FPS: %.2f", 1.0f / renderer.dt());
        const math3d::Vector3D& cam_position = camera.get_position();
        const math3d::Vector3D cam_forward = math3d::normalize(
            math3d::operator-(camera.get_target(), cam_position));
        window->widgets()->text(
            "Camera position: (%.3f, %.3f, %.3f)",
            cam_position[0], cam_position[1], cam_position[2]);
        window->widgets()->text(
            "Camera forward: (%.3f, %.3f, %.3f)",
            cam_forward[0], cam_forward[1], cam_forward[2]);
        if (gltf_loader != nullptr) {
            const Scene& scene = gltf_loader->get_scene();
            window->widgets()->text("Primitives: %zu", scene.primitives().size());
            window->widgets()->text("Visible primitives: %zu", scene.visible_primitive_indices().size());
        }
        window->widgets()->pop_window();
    });

    renderer.set_loading_gui_impl_callback([&](const CommandBuffer& cmd_buffer, double dt) {
        imgui_renderer.render_loading(cmd_buffer, dt);
    });
    renderer.set_render_gui_impl_callback([&](const CommandBuffer& cmd_buffer) {
        imgui_renderer.render(cmd_buffer);
    });
    renderer.set_render_task_end_callback([&]() {
        imgui_renderer.cleanup();
        window->remove_event_listener(&camera);
        if (gltf_loader != nullptr) {
            ocarina::delete_with_allocator<GltfAsyncLoader>(gltf_loader);
            gltf_loader = nullptr;
        }
    });

    renderer.run();
    window->run([](double) {});
    renderer.shutdown();

    return 0;
}
