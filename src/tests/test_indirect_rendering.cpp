#include "core/stl.h"
#include "core/logging.h"
#include "math/basic_types.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include "rhi/shader_program_key.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/pso_request.h"
#include "framework/frame_resources.h"
#include "framework/scene.h"
#include "framework/pass_group_id.h"
#include "framework/mesh.h"
#include "framework/indirect_draw_resources.h"
#include "framework/internal_textures.h"
#include "framework/bindless_texture_registry.h"
#include "framework/pipeline_manager.h"
#include "framework/global_gpu_storage.h"
#include "framework/math.h"
#include "rhi/bindless_sampler.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/descriptor_set.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "rhi/pipeline_state.h"
#include <algorithm>

using namespace ocarina;

namespace {

constexpr uint32_t kGridCount = 50;
constexpr float kGridSpacing = 2.0f;
constexpr uint32_t kTotalCubes = kGridCount * kGridCount * kGridCount;
constexpr float kCubeHalfExtent = 0.5f;

void apply_mesh_material_defaults(Material* material) {
    if (material == nullptr) {
        return;
    }
    material->set_property("baseColorFactor", make_float4(1.f, 1.f, 1.f, 1.f));
    material->set_property("roughness", 1.f);
    material->set_property("metallic", 0.f);
    material->set_property("ao", 1.f);
    material->set_property("normalIndex", 0u);
    material->set_property("normalSamplerIndex", 0u);
    material->set_property("metallicRoughnessIndex", InvalidUI32);
    material->set_property("metallicRoughnessSamplerIndex", 0u);
}

void apply_mesh_bindless_indices(Material* material, const TextureHandle& albedo_handle) {
    if (material == nullptr) {
        return;
    }
    material->set_property(
        "albedoIndex",
        &albedo_handle.bindless_index_,
        sizeof(albedo_handle.bindless_index_));
    material->set_property(
        "albedoSamplerIndex",
        get_bindless_sampler_index(
            TextureSampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT}));
}

/// Orthographic view-projection that fits every cube in the 50^3 grid, with padding.
/// Returned matrix is transposed for the column-major HLSL push constant.
math3d::Matrix4 make_visible_debug_view_proj() {
    const float grid_max = static_cast<float>(kGridCount - 1) * kGridSpacing;
    const math3d::Vector3D bounds_min{-kCubeHalfExtent, -kCubeHalfExtent, -kCubeHalfExtent};
    const math3d::Vector3D bounds_max{
        grid_max + kCubeHalfExtent,
        grid_max + kCubeHalfExtent,
        grid_max + kCubeHalfExtent};
    const math3d::Vector3D center{
        (bounds_min[0] + bounds_max[0]) * 0.5f,
        (bounds_min[1] + bounds_max[1]) * 0.5f,
        (bounds_min[2] + bounds_max[2]) * 0.5f};
    // Sit above the top cubes and look slightly down toward the grid center.
    constexpr float kHeightAboveTop = 180.0f;
    constexpr float kHorizontalOffset = 120.0f;
    const math3d::Vector3D eye{
        center[0] - kHorizontalOffset,
        bounds_max[1] + kHeightAboveTop,
        center[2] - kHorizontalOffset};
    const math3d::Matrix4 view = math3d::Matrix4::look_at(eye, center, math3d::Vector3D{0.0f, 1.0f, 0.0f});

    float min_x = bounds_max[0];
    float max_x = bounds_min[0];
    float min_y = bounds_max[0];
    float max_y = bounds_min[0];
    float min_z = bounds_max[0];
    float max_z = bounds_min[0];
    const float axes[2] = {bounds_min[0], bounds_max[0]};
    for (float x : axes) {
        for (float y : axes) {
            for (float z : axes) {
                const float view_x = view(0, 0) * x + view(0, 1) * y + view(0, 2) * z + view(0, 3);
                const float view_y = view(1, 0) * x + view(1, 1) * y + view(1, 2) * z + view(1, 3);
                const float view_z = view(2, 0) * x + view(2, 1) * y + view(2, 2) * z + view(2, 3);
                min_x = std::min(min_x, view_x);
                max_x = std::max(max_x, view_x);
                min_y = std::min(min_y, view_y);
                max_y = std::max(max_y, view_y);
                min_z = std::min(min_z, view_z);
                max_z = std::max(max_z, view_z);
            }
        }
    }

    const float span = std::max(max_x - min_x, max_y - min_y);
    const float pad = span * 0.08f + 2.0f;
    const float half = span * 0.5f + pad;
    const float mid_x = (min_x + max_x) * 0.5f;
    const float mid_y = (min_y + max_y) * 0.5f;
    const float z_pad = (max_z - min_z) * 0.08f + 1.0f;
    math3d::Matrix4 projection = math3d::Matrix4::ortho(
        mid_x - half,
        mid_x + half,
        mid_y - half,
        mid_y + half,
        min_z - z_pad,
        max_z + z_pad);
    // Same Vulkan NDC Y flip as Camera::calculate_projection_matrix().
    // Without it, a camera above the grid appears to look up from below.
    projection(1, 1) = -projection(1, 1);
    return (projection * view).transpose();
}

}// namespace

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();
    context.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Indirect Rendering - 50x50x50 Cubes", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path shader_dir = project_root / "res/shaderlibrary/builtin";
    const ocarina_string shader_vert = ocarina::to_ocarina_string(fs::absolute(shader_dir / "mesh.vert").string());
    const ocarina_string shader_frag = ocarina::to_ocarina_string(fs::absolute(shader_dir / "mesh.frag").string());
    const ocarina_string cull_shader = ocarina::to_ocarina_string(fs::absolute(shader_dir / "indirect_cull.compute").string());
    const ocarina_string scan_shader = ocarina::to_ocarina_string(fs::absolute(shader_dir / "indirect_scan.compute").string());
    const ocarina_string compact_shader = ocarina::to_ocarina_string(fs::absolute(shader_dir / "indirect_compact.compute").string());
    const ocarina_string debug_vert = ocarina::to_ocarina_string(fs::absolute(shader_dir / "indirect_visible_debug.vert").string());
    const ocarina_string debug_frag = ocarina::to_ocarina_string(fs::absolute(shader_dir / "indirect_visible_debug.frag").string());

    Material* material = nullptr;
    Mesh* cube_mesh = nullptr;
    Scene* scene = nullptr;
    ShaderProgram* debug_program = nullptr;
    TextureHandle white_handle{};
    IndirectDrawResources indirect;
    bool indices_bound = false;
    bool gpu_culling_enabled = true;
    bool show_visible_entities = false;
    const math3d::Matrix4 visible_view_proj = make_visible_debug_view_proj();

    Renderer renderer(&device);

    ocarina_set<string> vertex_options;
    vertex_options.insert(make_shader_option("INDIRECT_INSTANCE", 1));
    ocarina_set<string> pixel_options;

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        [&, shader_vert, shader_frag, cull_shader, scan_shader, compact_shader, debug_vert, debug_frag](Device* load_device) {
            ResourceManager& resources = ResourceManager::instance();
            ocarina_set<string> debug_options;
            ShaderProgram* program = resources.create_shader_program(
                load_device, shader_vert, shader_frag, vertex_options, pixel_options);
            debug_program = resources.create_shader_program(
                load_device, debug_vert, debug_frag, debug_options, debug_options);
            material = resources.create_material(load_device, program);
            cube_mesh = Mesh::create_cube();
            white_handle = InternalTextures::instance().get_white_texture_handle(load_device);
            apply_mesh_material_defaults(material);
            apply_mesh_bindless_indices(material, white_handle);

            scene = ocarina::new_with_allocator<Scene>();
            scene->reserve_entities(kTotalCubes);
            EntityComponentSystem& ecs = EntityComponentSystem::instance();

            uint32_t base_entity = 0;
            for (uint32_t height = 0; height < kGridCount; ++height) {
                for (uint32_t row = 0; row < kGridCount; ++row) {
                    for (uint32_t col = 0; col < kGridCount; ++col) {
                        const uint32_t scene_index = scene->emplace_entity();
                        const uint32_t entity = scene->entity_index(scene_index);
                        if (scene_index == 0) {
                            base_entity = entity;
                        }
                        ecs.add_transform_component(entity);
                        scene->transform_component(scene_index).set_position(make_float3(
                            static_cast<float>(col) * kGridSpacing,
                            static_cast<float>(height) * kGridSpacing,
                            static_cast<float>(row) * kGridSpacing));
                    }
                }
            }

            // Transforms live in the ECS / FrameResources g_transforms buffer.
            // Only the cull scratch and the indirect args are owned here.
            if (!indirect.initialize(
                    load_device,
                    kTotalCubes,
                    base_entity,
                    kCubeHalfExtent,
                    cull_shader,
                    scan_shader,
                    compact_shader)) {
                OC_ERROR("Failed to initialize indirect draw resources");
            }
        });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(1280.0f / 720.0f);
    camera.set_znear(0.1f);
    camera.set_zfar(2000.0f);
    camera.set_position({-80.0f, 0.0f, -80.0f});
    camera.set_target({99.0f, 0.0f, 99.0f});

    constexpr uint32_t kVisibleTextureSize = 512;
    const TextureUsageFlags visible_color_usage = static_cast<TextureUsageFlags>(
        static_cast<uint32_t>(TextureUsageFlags::RenderTarget)
        | static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly));
    Texture* visible_color = ResourceManager::instance().create_render_target_texture(
        &device,
        "indirect_visible_color",
        kVisibleTextureSize,
        kVisibleTextureSize,
        PixelStorage::BYTE4,
        visible_color_usage);
    Texture* visible_depth = ResourceManager::instance().create_render_target_texture(
        &device,
        "indirect_visible_depth",
        kVisibleTextureSize,
        kVisibleTextureSize,
        PixelStorage::FLOAT1,
        TextureUsageFlags::DepthStencil);
    RenderTarget visible_target = RenderTarget::texture(visible_color, visible_depth);
    RenderPassCreation visible_pass_creation;
    visible_pass_creation.render_target = &visible_target;
    visible_pass_creation.clear_color = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    visible_pass_creation.clear_depth = 1.0f;
    visible_pass_creation.clear_stencil = 0;
    visible_pass_creation.present_swapchain = false;
    RHIRenderPass* visible_pass = device.create_render_pass(visible_pass_creation);
    visible_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (!show_visible_entities || !indirect.ready() || !indirect.has_draw_args()
            || material == nullptr || cube_mesh == nullptr || debug_program == nullptr) {
            return;
        }
        if (cube_mesh->gpu_resource_state() != GPUResourceState::GPU_Ready || !material->is_renderable()) {
            return;
        }

        const PipelineState pipeline_state = PipelineState::MakeGraphicsDefault(debug_program);
        RHIPipeline* pipeline = PipelineManager::instance().get_pipeline(pipeline_state, visible_pass);
        if (pipeline == nullptr) {
            return;
        }

        if (!indices_bound && material->has_material_descriptor_set()) {
            indirect.bind_visible_indices(material->get_material_descriptor_set());
            indices_bound = true;
        }

        const MeshGeometrySlice& geometry = cube_mesh->geometry_slice();
        VertexBuffer* vertex_buffer = GlobalGPUStorage::instance().vertex_buffer(geometry.vertex_page);
        IndexBuffer* index_buffer = GlobalGPUStorage::instance().index_buffer(geometry.index_page);
        if (vertex_buffer == nullptr || index_buffer == nullptr) {
            return;
        }

        cmd.bind_pipeline(pipeline);
        FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);
        if (material->has_material_descriptor_set()) {
            DescriptorSet* material_set = material->get_material_descriptor_set();
            cmd.bind_descriptor_sets(
                &material_set,
                material->material_descriptor_set_index(),
                1,
                pipeline->pipeline_layout);
        }
        cmd.set_vertex_buffer(vertex_buffer);
        cmd.set_index_buffer(index_buffer);
        cmd.push_constants(visible_view_proj.m.data(), 0, static_cast<uint32_t>(sizeof(float) * visible_view_proj.m.size()), 0);
        cmd.draw_indexed_indirect(indirect.draw_args_handle(), 0, 1, indirect.draw_args_stride());
    });

    RenderTarget swapchain_target = RenderTarget::swapchain();

    RenderPassCreation compute_pass_creation;
    compute_pass_creation.render_target = nullptr;
    RHIRenderPass* compute_pass = device.create_render_pass(compute_pass_creation);
    compute_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (!indirect.ready() || cube_mesh == nullptr) {
            return;
        }
        indirect.try_set_draw_mesh(cube_mesh);
        if (!indirect.has_draw_args()) {
            return;
        }
        const math3d::Matrix4 view_projection =
            camera.get_projection_matrix() * camera.get_view_matrix();
        indirect.record(cmd, Frustum(view_projection), gpu_culling_enabled);
    });

    RenderPassCreation render_pass_creation;
    render_pass_creation.render_target = &swapchain_target;
    render_pass_creation.clear_color = make_float4(0.08f, 0.08f, 0.1f, 1.0f);
    render_pass_creation.clear_depth = 1.0f;
    render_pass_creation.clear_stencil = 0;
    render_pass_creation.present_swapchain = false;
    RHIRenderPass* render_pass = device.create_render_pass(render_pass_creation);
    render_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (!indirect.ready() || !indirect.has_draw_args() || material == nullptr || cube_mesh == nullptr) {
            return;
        }
        if (cube_mesh->gpu_resource_state() != GPUResourceState::GPU_Ready || !material->is_renderable()) {
            return;
        }

        const PipelineState* pipeline_state = material->pipeline_state_for_pass(PassGroupId::Opaque);
        if (pipeline_state == nullptr) {
            return;
        }
        RHIPipeline* pipeline = PipelineManager::instance().get_pipeline(*pipeline_state, render_pass);
        if (pipeline == nullptr) {
            return;
        }

        if (!indices_bound && material->has_material_descriptor_set()) {
            indirect.bind_visible_indices(material->get_material_descriptor_set());
            indices_bound = true;
        }

        const MeshGeometrySlice& geometry = cube_mesh->geometry_slice();
        VertexBuffer* vertex_buffer = GlobalGPUStorage::instance().vertex_buffer(geometry.vertex_page);
        IndexBuffer* index_buffer = GlobalGPUStorage::instance().index_buffer(geometry.index_page);
        if (vertex_buffer == nullptr || index_buffer == nullptr) {
            return;
        }

        cmd.bind_pipeline(pipeline);
        FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);
        if (material->has_material_descriptor_set()) {
            DescriptorSet* material_set = material->get_material_descriptor_set();
            cmd.bind_descriptor_sets(
                &material_set,
                material->material_descriptor_set_index(),
                1,
                pipeline->pipeline_layout);
        }
        cmd.set_vertex_buffer(vertex_buffer);
        cmd.set_index_buffer(index_buffer);
        cmd.draw_indexed_indirect(indirect.draw_args_handle(), 0, 1, indirect.draw_args_stride());
    });

    RenderPassCreation ui_pass_creation;
    ui_pass_creation.render_target = &swapchain_target;
    ui_pass_creation.clear_color_attachment = false;
    ui_pass_creation.clear_depth_attachment = false;
    ui_pass_creation.enable_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    ocarina_set<string> debug_options;
    async_loader.set_pso_requests({
        PSORequest::make_graphics(shader_vert, shader_frag, render_pass, vertex_options, pixel_options),
        PSORequest::make_graphics(debug_vert, debug_frag, visible_pass, debug_options, debug_options),
    });

    renderer.set_camera(&camera);
    renderer.set_frustum_culling_enabled(false);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const handle_ty visible_imgui_texture = device.imgui_add_texture(visible_color);
    const string window_name = "Indirect Rendering";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    frame_info.extra = [&](Widgets& widgets) {
        widgets.check_box("GPU frustum culling", &gpu_culling_enabled);
        widgets.text("Cubes: %u", kTotalCubes);
        widgets.text("Culling: %s", gpu_culling_enabled ? "enabled" : "disabled");
        widgets.use_window("Visible entities", [&] {
            const char* button_label = show_visible_entities ? "Hide visible entities" : "Show visible entities";
            if (widgets.button(button_label)) {
                show_visible_entities = !show_visible_entities;
            }
            if (show_visible_entities && visible_imgui_texture != 0) {
                widgets.image(
                    visible_imgui_texture,
                    make_uint2(kVisibleTextureSize, kVisibleTextureSize));
            }
        });
    };
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.dt());
    });

    renderer.pass_group(PassGroupId::ComputePass).add_render_pass(compute_pass);
    renderer.pass_group(PassGroupId::Offscreen).add_render_pass(visible_pass);
    renderer.pass_group(PassGroupId::Opaque).add_render_pass(render_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    renderer.set_async_loader(&async_loader, nullptr, [&]() {
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
        // The last frame's command buffer may still be sampling this ImGui descriptor set.
        device.wait_idle();
        if (visible_imgui_texture != 0) {
            device.imgui_remove_texture(visible_imgui_texture);
        }
        imgui_renderer.cleanup();
        window->remove_event_listener(&camera);
        indirect.release();
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
