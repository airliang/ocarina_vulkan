#include "core/stl.h"
#include "core/hash.h"
#include "math/basic_types.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/primitive.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/frame_resources.h"
#include "framework/scene.h"
#include "framework/transform.h"
#include "framework/mesh.h"
#include "framework/internal_textures.h"
#include "framework/bindless_texture_registry.h"
#include "rhi/bindless_sampler.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"

using namespace ocarina;

struct GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix;
    math3d::Matrix4 view_matrix;
    float4 camera_pos;
    float4 light_pos;
};

namespace {

constexpr uint32_t kGridCount = 50;
constexpr float kGridSpacing = 2.0f;
constexpr uint32_t kTotalCubes = kGridCount * kGridCount * kGridCount;

}// namespace

static void apply_mesh_material_defaults(Primitive& primitive) {
    primitive.set_material_parameter("baseColorFactor", make_float4(1.f, 1.f, 1.f, 1.f));
    primitive.set_material_parameter("roughness", 1.f);
    primitive.set_material_parameter("metallic", 0.f);
    primitive.set_material_parameter("ao", 1.f);
    primitive.set_material_parameter("normalIndex", 0u);
    primitive.set_material_parameter("normalSamplerIndex", 0u);
}

static void apply_mesh_bindless_indices(Primitive& primitive) {
    const Primitive::TextureHandle albedo_handle = primitive.get_texture_handle(hash64("albedo"));
    primitive.set_material_parameter("albedoIndex", albedo_handle.bindless_index_);
    OC_ASSERT(albedo_handle.texture_ != nullptr);
    primitive.set_material_parameter(
        "albedoSamplerIndex",
        get_bindless_sampler_index(*albedo_handle.texture_));
}

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Culling Test - 100x100x100 Cubes", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path src_root = source_dir.parent_path();
    const fs::path shader_vert = src_root / "backends/vulkan/builtin/mesh.vert";
    const fs::path shader_frag = src_root / "backends/vulkan/builtin/mesh.frag";

    Material* material = nullptr;
    Mesh* cube_mesh = nullptr;
    Scene* scene = nullptr;
    Texture* white_texture = nullptr;

    Renderer renderer(&device);

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
        cube_mesh = Mesh::create_cube();
        white_texture = InternalTextures::instance().get_white_texture(load_device);
        BindlessTextureRegistry::instance().allocate_index(white_texture);

        scene = ocarina::new_with_allocator<Scene>();
        scene->reserve_primitives(kTotalCubes);

        for (uint32_t height = 0; height < kGridCount; ++height) {
            for (uint32_t row = 0; row < kGridCount; ++row) {
                for (uint32_t col = 0; col < kGridCount; ++col) {
                    Primitive& primitive = scene->emplace_primitive();
                    const uint32_t primitive_index = scene->primitive_count() - 1;
                    scene->transform_component(primitive_index).set_position(make_float3(
                        static_cast<float>(col) * kGridSpacing,
                        static_cast<float>(height) * kGridSpacing,
                        static_cast<float>(row) * kGridSpacing));
                    primitive.set_mesh(cube_mesh);
                    primitive.set_material(material);
                    apply_mesh_material_defaults(primitive);
                    primitive.add_bindless_texture(hash64("albedo"), white_texture);
                    primitive.add_sampler(hash64("sampler_albedo"), *white_texture->get_sampler_pointer());
                    apply_mesh_bindless_indices(primitive);
                }
            }
        }

        scene->build_grid();
    });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(1280.0f / 720.0f);
    camera.set_znear(0.1f);
    camera.set_zfar(2000.0f);
    camera.set_position({-80.0f, 120.0f, -80.0f});
    camera.set_target({99.0f, 99.0f, 99.0f});

    const uint64_t model_matrix_name_id = hash64("modelMatrix");
    const uint64_t model_matrix_inverse_name_id = hash64("modelMatrixInverse");
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

    RenderPassCreation render_pass_creation;
    render_pass_creation.swapchain_clear_color = make_float4(0.08f, 0.08f, 0.1f, 1.0f);
    render_pass_creation.swapchain_clear_depth = 1.0f;
    render_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass* render_pass = device.create_render_pass(render_pass_creation);

    bool frustum_culling_enabled = true;

    renderer.set_camera(&camera);
    renderer.set_frustum_culling_enabled(frustum_culling_enabled);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Culling Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    frame_info.extra = [&](Widgets& widgets) {
        widgets.check_box("Frustum culling", &frustum_culling_enabled);
        if (scene != nullptr) {
            widgets.text("Total grids: %u", scene->grid_cell_count());
            widgets.text("Visible grids: %u", scene->visible_grid_count());
            const auto& visible_grids = scene->visible_grid_indices();
            const uint32_t sample_count = static_cast<uint32_t>(std::min<size_t>(visible_grids.size(), 12));
            for (uint32_t i = 0; i < sample_count; ++i) {
                const uint32_t flat = visible_grids[i];
                const auto [cx, cz] = scene->grid_cell_coords(flat);
                widgets.text(
                    "Grid[%u] cell=(%d,%d) prim=%u",
                    flat,
                    cx,
                    cz,
                    scene->grid_cell_primitive_count(flat));
            }
            widgets.text(
                "Culling: %s",
                frustum_culling_enabled ? "enabled" : "disabled");
        }
    };
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.loading_dt());
    });

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        if (scene == nullptr) {
            return;
        }

        for (uint32_t index = 0; index < scene->primitive_count(); ++index) {
            Primitive& primitive = scene->primitive(index);
            primitive.set_update_push_constant_function(update_push_constant);
            primitive.set_geometry_data_setup(&device, [&](Primitive& prim) {
                if (material != nullptr) {
                    material->set_target_render_pass(render_pass);
                }
                (void)prim;
            });
        }
        renderer.set_scene(scene);

        imgui_renderer.set_frame_callback([&]() {
            display_frame_info(*window->widgets());
        });
    });

    FrameResources::instance().set_update_callback([&](FrameResources&, double dt) {
        (void)dt;
        renderer.set_frustum_culling_enabled(frustum_culling_enabled);

        DescriptorSet* global_descriptor_set = FrameResources::instance().get_global_descriptor_set("global_ubo");
        const math3d::Vector3D& cam_position = camera.get_position();
        GlobalUniformBuffer global_ubo_data = {
            camera.get_projection_matrix().transpose(),
            camera.get_view_matrix().transpose(),
            make_float4(cam_position[0], cam_position[1], cam_position[2], 1.0f),
            make_float4(200.0f, 300.0f, 200.0f, 1.0f)};
        global_descriptor_set->update_buffer(hash64("global_ubo"), &global_ubo_data, sizeof(GlobalUniformBuffer));

        render_pass->clear_draw_call_items();
        if (scene != nullptr) {
            renderer.ensure_render_components(scene->primitive_count());
            for (uint32_t primitive_index : scene->visible_primitive_indices()) {
                Primitive& primitive = scene->primitive(primitive_index);
                primitive.update_render_component(
                    &device,
                    renderer.ecs().render_component(primitive_index),
                    scene->transform_component(primitive_index));
                RenderComponent& render_component = renderer.ecs().render_component(primitive_index);
                render_pass->add_draw_call(primitive_index, render_component.pipeline);
            }
        }
    });

    renderer.add_render_pass(render_pass);

    renderer.set_loading_gui_impl_callback([&](const CommandBuffer& cmd_buffer) {
        imgui_renderer.render(cmd_buffer);
    });
    renderer.set_render_gui_impl_callback([&](const CommandBuffer& cmd_buffer) {
        imgui_renderer.render(cmd_buffer);
    });
    renderer.set_render_task_end_callback([&]() {
        imgui_renderer.cleanup();
        window->remove_event_listener(&camera);
        if (scene != nullptr) {
            ocarina::delete_with_allocator<Scene>(scene);
            scene = nullptr;
        }
        InternalTextures::instance().cleanup();
    });

    renderer.run();
    window->run([](double) {});
    renderer.shutdown();

    return 0;
}
