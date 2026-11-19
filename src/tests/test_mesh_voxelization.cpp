//
// Mesh voxelization test:
// - Open glTF via File menu (same loading progress UX as alphablend / PBR).
// - Voxelize window: voxel size (meters) + "Voxelize" button.
// - After load, voxel size defaults to min(AABB axes) / 10.
// - Voxelize merges all submeshes into one VoxelWorld and replaces the scene mesh.
//

#include "core/stl.h"
#include "core/logging.h"
#include "core/string_util.h"
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
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/internal_textures.h"
#include "framework/bindless_texture_registry.h"
#include "framework/widgets.h"
#include "rhi/bindless_sampler.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "voxel/mesh_voxelizer.h"
#include "voxel/voxel_world.h"

#include <algorithm>
#include <cmath>

using namespace ocarina;

namespace {

void apply_vertex_color_material_defaults(Material* material, const TextureHandle& white) {
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
    material->set_property(
        "albedoIndex",
        &white.bindless_index_,
        sizeof(white.bindless_index_));
    material->set_property(
        "albedoSamplerIndex",
        get_bindless_sampler_index(
            TextureSampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT}));
}

void frame_camera_to_bounds(Camera& camera, const BoundingBox& bounds) {
    if (!bounds.valid) {
        return;
    }
    const float3 center = (bounds.min + bounds.max) * 0.5f;
    const float3 extent = bounds.max - bounds.min;
    const float radius = std::max({extent.x, extent.y, extent.z, 0.25f});
    camera.set_target(ktm::fvec3{center.x, center.y, center.z});
    camera.set_position(ktm::fvec3{
        center.x,
        center.y + radius * 0.35f,
        center.z + radius * 1.8f});
    camera.set_move_speed(std::max(0.5f, radius * 0.5f));
}

}// namespace

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();
    context.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Mesh Voxelization Test", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path repo_root = source_dir.parent_path().parent_path();
    const fs::path default_gltf =
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
    ui_pass_creation.enable_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    const ocarina_string shader_vert_abs =
        ocarina::to_ocarina_string(fs::absolute(shader_vert).string());
    const ocarina_string shader_frag_abs =
        ocarina::to_ocarina_string(fs::absolute(shader_frag).string());

    fs::path current_gltf = default_gltf;
    float voxel_size_meters = 0.1f;
    bool mesh_loaded = false;
    bool showing_voxels = false;
    ocarina_string status_message = ocarina::to_ocarina_string("Load a glTF, then Voxelize.");
    BoundingBox mesh_bounds{};

    VoxelWorld voxel_world;
    Material* voxel_material = nullptr;

    GltfAsyncLoader gltf_loader(
        &renderer.task_scheduler(),
        &device,
        &renderer,
        shader_vert_abs,
        shader_frag_abs,
        ocarina::to_ocarina_string(fs::absolute(current_gltf).string()));
    gltf_loader.set_progress_listener(&loading_progress);

    auto ensure_voxel_material = [&]() {
        if (voxel_material != nullptr) {
            return true;
        }
        ResourceManager& resources = ResourceManager::instance();
        ocarina_set<string> options;
        ShaderProgram* program = resources.create_shader_program(
            &device, shader_vert_abs, shader_frag_abs, options, options);
        if (program == nullptr) {
            status_message = ocarina::to_ocarina_string("Failed to create voxel shader program");
            return false;
        }
        voxel_material = resources.create_unique_material(&device, program);
        if (voxel_material == nullptr) {
            status_message = ocarina::to_ocarina_string("Failed to create voxel material");
            return false;
        }
        const TextureHandle white =
            InternalTextures::instance().get_white_texture_handle(&device);
        apply_vertex_color_material_defaults(voxel_material, white);
        return true;
    };

    auto on_gltf_loaded = [&]() {
        Scene& scene = gltf_loader.get_scene();
        for (uint32_t index = 0; index < scene.primitive_count(); ++index) {
            scene.render_component(index).set_update_push_constant_function(
                [](RenderComponent& render, uint32_t entity_index) {
                    render.set_push_constant_variable("transform_index", entity_index);
                });
        }
        renderer.set_scene(&scene);

        mesh_bounds = MeshVoxelizer::compute_gltf_bounds(current_gltf);
        voxel_size_meters = MeshVoxelizer::suggest_voxel_size(mesh_bounds);
        frame_camera_to_bounds(camera, mesh_bounds);
        mesh_loaded = true;
        showing_voxels = false;
        status_message = ocarina::format(
            "Loaded mesh. Suggested voxel size {:.4f} m",
            voxel_size_meters);
    };

    auto open_gltf_scene = [&]() {
        if (renderer.is_async_loading()) {
            return;
        }

        FileDialogFilterVec filters;
        filters.emplace_back("gltf", "glTF");
        filters.emplace_back("glb", "glTF Binary");
        fs::path selected;
        if (!Widgets::open_file_dialog(selected, filters)) {
            return;
        }

        // File dialog returns during UI pass recording. Defer buffer destruction until
        // before the next frame — destroying mid-CB invalidates the recording buffer.
        const fs::path selected_abs = fs::absolute(selected);
        renderer.enqueue_before_frame([&, selected_abs]() {
            device.wait_idle();
            current_gltf = selected_abs;
            mesh_loaded = false;
            showing_voxels = false;
            voxel_world.clear();
            renderer.set_scene(nullptr);
            loading_progress.reset();
            gltf_loader.reset_for_reload(
                ocarina::to_ocarina_string(selected_abs.string()));
            gltf_loader.set_progress_listener(&loading_progress);
            renderer.set_async_loader(&gltf_loader, nullptr, on_gltf_loaded);
            renderer.start_async_loader();
        });
    };

    auto run_voxelize = [&]() {
        if (!mesh_loaded || renderer.is_async_loading()) {
            status_message = ocarina::to_ocarina_string("Load a glTF mesh before voxelizing.");
            return;
        }
        if (!ensure_voxel_material()) {
            return;
        }

        const float size = std::max(voxel_size_meters, 1.0e-5f);
        status_message = ocarina::format("Voxelizing at {:.4f} m ...", size);
        const MeshVoxelizerResult result =
            MeshVoxelizer::voxelize_gltf(current_gltf, size, voxel_world, 1);
        if (!result.ok) {
            status_message = result.error.empty()
                ? ocarina::to_ocarina_string("Voxelization failed")
                : result.error;
            return;
        }

        Scene& scene = gltf_loader.get_scene();
        voxel_world.publish_to_scene(scene, voxel_material);
        scene.build_grid();
        renderer.set_scene(&scene);
        showing_voxels = true;
        status_message = ocarina::format(
            "Voxelized: {} tris → {} voxels ({} chunks)",
            result.triangle_count,
            result.solid_voxel_count,
            voxel_world.chunk_count());
    };

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Mesh Voxelization Test";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);

    imgui_renderer.set_frame_callback([&]() {
        Widgets& widgets = *window->widgets();
        widgets.use_main_menu_bar([&]() {
            widgets.use_menu("File", [&]() {
                widgets.set_enabled(!renderer.is_async_loading(), [&]() {
                    if (widgets.menu_item("Open glTF...")) {
                        open_gltf_scene();
                    }
                });
            });
        });

        if (renderer.is_async_loading()) {
            display_loading_progress(widgets, &loading_progress, renderer.dt());
        } else {
            display_frame_info(widgets);
        }

        widgets.use_window("Voxelize", [&]() {
            widgets.text(status_message);
            widgets.text(ocarina::format(
                "Mesh: {}",
                current_gltf.filename().string()));
            if (mesh_bounds.valid) {
                const float3 extent = mesh_bounds.max - mesh_bounds.min;
                widgets.text(ocarina::format(
                    "AABB min-axis: {:.4f} m",
                    std::min({extent.x, extent.y, extent.z})));
            }
            widgets.drag_float("Voxel size (m)", &voxel_size_meters, 0.001f, 0.0001f, 10.0f, "%.4f");
            widgets.set_enabled(mesh_loaded && !renderer.is_async_loading(), [&]() {
                if (widgets.button("Voxelize")) {
                    run_voxelize();
                }
            });
            if (showing_voxels) {
                widgets.text(ocarina::format(
                    "Showing voxels (solid={})",
                    voxel_world.solid_voxel_count()));
            }
        });
    });

    renderer.set_render_pass_primitive_filter(
        [opaque_pass, transparent_pass](uint32_t, RHIRenderPass* pass) {
            return pass == opaque_pass || pass == transparent_pass;
        });
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    renderer.set_async_loader(&gltf_loader, nullptr, on_gltf_loaded);

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
