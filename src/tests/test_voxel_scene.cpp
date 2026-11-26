//
// Voxel Perlin-noise scene:
// - Parallel 64^3 Perlin FBM → NoiseTexture3D → VoxelWorldData → VoxelWorld::create_from_data.
// - Chunk meshes built in parallel via enkiTS, vertex-color albedo (voxel_mesh).
// - Godot-like procedural sky (procedural_sky.frag); no cubemap.
//

#include "core/stl.h"
#include "core/logging.h"
#include "core/noise.h"
#include "core/string_util.h"
#include "math/basic_types.h"
#include "framework/math.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include "rhi/command_buffer.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/scene.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "framework/async_loader.h"
#include "framework/loading_progress_listener.h"
#include "framework/pso_request.h"
#include "framework/pass_group_id.h"
#include "framework/render_component.h"
#include "voxel/voxel_world.h"
#include "voxel/voxel_world_data.h"
#include "ext/enkiTS/src/TaskScheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace ocarina;

namespace {

constexpr uint32_t kWorldSize = 64;
constexpr float kVoxelSizeMeters = 1.0f;
constexpr float kNoiseFrequency = 1.0f / 16.0f;

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

/// Godot ProceduralSkyMaterial defaults (energy already folded into colors).
void apply_procedural_sky_defaults(Material* material) {
    if (material == nullptr) {
        return;
    }
    constexpr float kSkyCurve = 0.15f;
    constexpr float kGroundCurve = 0.02f;
    constexpr float kSunCurve = 0.15f;
    constexpr float kSunAngleMaxDeg = 30.0f;
    constexpr float kSunAngularRadiusDeg = 0.5f;

    material->set_property("sky_top_color", make_float4(0.385f, 0.454f, 0.55f, 1.0f));
    material->set_property("sky_horizon_color", make_float4(0.6463f, 0.6558f, 0.6708f, 1.0f));
    material->set_property("ground_bottom_color", make_float4(0.2f, 0.169f, 0.133f, 1.0f));
    material->set_property("ground_horizon_color", make_float4(0.6463f, 0.6558f, 0.6708f, 1.0f));
    material->set_property("inv_sky_curve", 0.6f / kSkyCurve);
    material->set_property("inv_ground_curve", 0.6f / kGroundCurve);
    material->set_property(
        "sun_angle_max",
        std::cos(kSunAngleMaxDeg * (ktm::pi<float> / 180.0f)));
    material->set_property(
        "inv_sun_curve",
        1.6f / std::pow(kSunCurve, 1.4f));
    material->set_property(
        "sun_size",
        std::cos(kSunAngularRadiusDeg * (ktm::pi<float> / 180.0f)));
    material->set_property("exposure", 1.0f);
}

[[nodiscard]] double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
}

[[nodiscard]] float density_from_noise(uint32_t x, uint32_t y, uint32_t z) noexcept {
    const float3 p = make_float3(
        static_cast<float>(x) * kNoiseFrequency,
        static_cast<float>(y) * kNoiseFrequency,
        static_cast<float>(z) * kNoiseFrequency);
    const float noise = perlin_fbm_3d(p, 4, 2.0f, 0.5f);
    const float height_bias =
        (static_cast<float>(y) / static_cast<float>(kWorldSize) - 0.42f) * 1.7f;
    return noise - height_bias;
}

NoiseTexture3D generate_perlin_noise_texture(enki::TaskScheduler& scheduler) {
    NoiseTexture3D texture;
    texture.size = make_uint3(kWorldSize, kWorldSize, kWorldSize);
    texture.density.resize(
        static_cast<size_t>(kWorldSize) * kWorldSize * kWorldSize);

    struct NoiseTask : enki::ITaskSet {
        NoiseTexture3D* texture = nullptr;
        void ExecuteRange(enki::TaskSetPartition range, uint32_t) override {
            for (uint32_t z = range.start; z < range.end; ++z) {
                for (uint32_t y = 0; y < kWorldSize; ++y) {
                    for (uint32_t x = 0; x < kWorldSize; ++x) {
                        const size_t index =
                            (static_cast<size_t>(z) * kWorldSize + y) * kWorldSize + x;
                        texture->density[index] = density_from_noise(x, y, z);
                    }
                }
            }
        }
    };

    NoiseTask task;
    task.texture = &texture;
    task.m_SetSize = kWorldSize;
    task.m_MinRange = 4;
    scheduler.AddTaskSetToPipe(&task);
    scheduler.WaitforTask(&task);
    return texture;
}

void build_voxel_world_from_noise(
    VoxelWorld& world,
    enki::TaskScheduler& scheduler,
    LoadingProgressListener* progress) {
    const auto total_start = std::chrono::steady_clock::now();

    if (progress != nullptr) {
        progress->set_phase(ocarina::to_ocarina_string("Generating 3D noise texture"));
        progress->set_progress(0.05f);
    }
    auto stage_start = std::chrono::steady_clock::now();
    NoiseTexture3D noise = generate_perlin_noise_texture(scheduler);
    const double noise_texture_ms = elapsed_ms(stage_start);

    if (progress != nullptr) {
        progress->set_phase(ocarina::to_ocarina_string("Building VoxelWorldData"));
        progress->set_progress(0.55f);
    }
    stage_start = std::chrono::steady_clock::now();
    VoxelWorldData world_data = build_voxel_world_data_from_noise_texture(
        noise,
        kVoxelSizeMeters,
        0.0f);
    // Density texture no longer needed after chunk occupancy is built.
    noise.density.clear();
    noise.density.shrink_to_fit();
    const double world_data_ms = elapsed_ms(stage_start);

    if (progress != nullptr) {
        progress->set_phase(ocarina::to_ocarina_string("create_from_data"));
        progress->set_progress(0.75f);
    }
    stage_start = std::chrono::steady_clock::now();
    world.create_from_data(world_data, make_float3(1.0f, 1.0f, 1.0f));
    const double create_from_data_ms = elapsed_ms(stage_start);
    const double fill_total_ms = elapsed_ms(total_start);

    std::printf(
        "[voxel-scene timing] noise → VoxelWorldData → VoxelWorld:\n"
        "  3D noise texture      %8.2f ms\n"
        "  build VoxelWorldData  %8.2f ms  (chunks=%zu)\n"
        "  create_from_data      %8.2f ms  (solid voxels=%u)\n"
        "  fill total            %8.2f ms\n",
        noise_texture_ms,
        world_data_ms,
        world_data.chunks.size(),
        create_from_data_ms,
        world.solid_voxel_count(),
        fill_total_ms);
    std::fflush(stdout);

    if (progress != nullptr) {
        progress->set_progress(0.9f);
    }
}

void frame_camera_to_bounds(Camera& camera, const BoundingBox& bounds) {
    if (!bounds.valid) {
        return;
    }
    const float3 center = (bounds.min + bounds.max) * 0.5f;
    const float3 extent = bounds.max - bounds.min;
    const float radius = std::max({extent.x, extent.y, extent.z, 1.0f});
    camera.set_target(ktm::fvec3{center.x, center.y, center.z});
    camera.set_position(ktm::fvec3{
        center.x,
        center.y + radius * 0.4f,
        center.z - radius * 1.2f});
    camera.set_move_speed(std::max(8.0f, radius * 0.35f));
    camera.set_zfar(std::max(500.0f, radius * 4.0f));
}

}// namespace

int main(int argc, char* argv[]) {
    RHIContext& context = RHIContext::instance();
    context.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("Voxel Perlin Scene", window_size);

    InstanceCreation instance_creation{};
    instance_creation.windowHandle = window->get_window_handle();
    instance_creation.windowWidth = window_size.x;
    instance_creation.windowHeight = window_size.y;
    Device device = context.create_device("vulkan", instance_creation);

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path repo_root = source_dir.parent_path().parent_path();
    // Same ForwardPBR path as test-pbr: DepthPrepass → Skybox → Opaque (Load color).
    const fs::path pipeline_path =
        repo_root / "res/pipelines/forward_pbr.pipeline.json";
    const fs::path voxel_vert = repo_root / "res/shaderlibrary/builtin/voxel_mesh.vert";
    const fs::path voxel_frag = repo_root / "res/shaderlibrary/builtin/voxel_mesh.frag";
    const fs::path skybox_vert = repo_root / "res/shaderlibrary/builtin/fullscreen.vert";
    const fs::path skybox_frag = repo_root / "res/shaderlibrary/builtin/procedural_sky.frag";

    const ocarina_string voxel_vert_abs =
        ocarina::to_ocarina_string(fs::absolute(voxel_vert).string());
    const ocarina_string voxel_frag_abs =
        ocarina::to_ocarina_string(fs::absolute(voxel_frag).string());
    const ocarina_string skybox_vert_abs =
        ocarina::to_ocarina_string(fs::absolute(skybox_vert).string());
    const ocarina_string skybox_frag_abs =
        ocarina::to_ocarina_string(fs::absolute(skybox_frag).string());

    LoadingProgressListener loading_progress;

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(
        static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    camera.set_position({32.0f, 48.0f, -20.0f});
    camera.set_target({32.0f, 20.0f, 32.0f});
    camera.set_move_speed(20.0f);
    camera.set_zfar(500.0f);

    Renderer renderer(&device);
    renderer.set_camera(&camera);
    renderer.set_loading_progress_listener(&loading_progress);

    RenderTarget swapchain_target = RenderTarget::swapchain();
    if (!renderer.load_default_pipeline(&swapchain_target, pipeline_path)) {
        OC_ERROR("Failed to load ForwardOpaqueTransparent pipeline");
        return -1;
    }

    RHIRenderPass* depth_prepass = renderer.find_pipeline_render_pass("DepthPrepass");
    RHIRenderPass* opaque_pass = renderer.find_pipeline_render_pass("Opaque");
    RHIRenderPass* transparent_pass = renderer.find_pipeline_render_pass("Transparent");
    if (depth_prepass == nullptr || opaque_pass == nullptr || transparent_pass == nullptr) {
        OC_ERROR("Pipeline missing DepthPrepass / Opaque / Transparent RHIRenderPass");
        return -1;
    }

    // DepthPrepass clears depth; skybox clears color only (same as test-pbr).
    RenderPassCreation skybox_pass_creation;
    skybox_pass_creation.render_target = &swapchain_target;
    skybox_pass_creation.clear_color = make_float4(0.02f, 0.02f, 0.02f, 1.0f);
    skybox_pass_creation.clear_depth = 1.0f;
    skybox_pass_creation.clear_stencil = 0;
    skybox_pass_creation.clear_depth_attachment = false;
    skybox_pass_creation.present_swapchain = false;
    RHIRenderPass* skybox_pass = device.create_render_pass(skybox_pass_creation);

    RenderPassCreation ui_pass_creation;
    ui_pass_creation.render_target = &swapchain_target;
    ui_pass_creation.clear_color_attachment = false;
    ui_pass_creation.clear_depth_attachment = false;
    ui_pass_creation.enable_depth_attachment = false;
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    Material* skybox_material = nullptr;
    Material* voxel_material = nullptr;
    Scene* scene = nullptr;
    VoxelWorld voxel_world;

    AsyncLoader scene_loader(
        &renderer.task_scheduler(),
        &device,
        [&](Device* load_device) {
            ResourceManager& resources = ResourceManager::instance();
            ocarina_set<string> options;

            ShaderProgram* skybox_program = resources.create_shader_program(
                load_device, skybox_vert_abs, skybox_frag_abs, options, options);
            skybox_material = resources.create_material(load_device, skybox_program);
            skybox_material->set_depth_stencil_state(PassGroupId::Opaque, make_skybox_depth_state());
            skybox_material->set_raster_state(PassGroupId::Opaque, make_skybox_raster_state());
            apply_procedural_sky_defaults(skybox_material);

            ShaderProgram* voxel_program = resources.create_shader_program(
                load_device, voxel_vert_abs, voxel_frag_abs, options, options);
            voxel_material = resources.create_unique_material(load_device, voxel_program);
            // Skip Equal-depth Opaque (needs DepthPrepass geometry); write depth here instead.
            DepthStencilState voxel_depth = DepthStencilState::Default();
            voxel_depth.depth_test_enable = true;
            voxel_depth.depth_write_enable = true;
            voxel_depth.depth_compare_op = SamplerCompareFunc::LE;
            voxel_material->set_depth_stencil_state(PassGroupId::Opaque, voxel_depth);

            loading_progress.begin(
                ocarina::to_ocarina_string("Voxel Perlin Scene"), kWorldSize);

            const auto scene_build_start = std::chrono::steady_clock::now();
            build_voxel_world_from_noise(
                voxel_world,
                renderer.task_scheduler(),
                &loading_progress);

            std::printf("[voxel-scene] meshing %u dirty chunks...\n", voxel_world.chunk_count());
            std::fflush(stdout);
            auto stage_start = std::chrono::steady_clock::now();
            voxel_world.rebuild_dirty_meshes(renderer.task_scheduler());
            const double mesh_ms = elapsed_ms(stage_start);
            std::printf("[voxel-scene] meshing done in %.2f ms\n", mesh_ms);
            std::fflush(stdout);

            stage_start = std::chrono::steady_clock::now();
            scene = ocarina::new_with_allocator<Scene>();
            voxel_world.publish_to_scene(*scene, voxel_material);
            const double publish_ms = elapsed_ms(stage_start);

            stage_start = std::chrono::steady_clock::now();
            scene->build_grid();
            const double build_grid_ms = elapsed_ms(stage_start);
            const double scene_build_total_ms = elapsed_ms(scene_build_start);

            std::printf(
                "[voxel-scene timing] after fill:\n"
                "  rebuild_dirty_meshes (parallel)  %8.2f ms  (chunks=%u)\n"
                "  publish_to_scene                 %8.2f ms  (merged=%u)\n"
                "  scene.build_grid                 %8.2f ms\n"
                "  scene build total                %8.2f ms\n",
                mesh_ms,
                voxel_world.chunk_count(),
                publish_ms,
                voxel_world.merged_render_count(),
                build_grid_ms,
                scene_build_total_ms);
            std::fflush(stdout);

            loading_progress.set_phase(ocarina::to_ocarina_string("Voxel scene ready"));
            loading_progress.complete();
        });

    PSORequest skybox_pso =
        PSORequest::make_graphics(skybox_vert_abs, skybox_frag_abs, skybox_pass);
    skybox_pso.raster_state = make_skybox_raster_state();
    skybox_pso.depth_stencil_state = make_skybox_depth_state();

    PSORequest voxel_pso =
        PSORequest::make_graphics(voxel_vert_abs, voxel_frag_abs, opaque_pass);
    scene_loader.set_pso_requests({std::move(skybox_pso), std::move(voxel_pso)});
    scene_loader.set_target_render_pass(opaque_pass);
    scene_loader.set_progress_listener(&loading_progress);

    renderer.set_render_pass_primitive_filter(
        [depth_prepass, opaque_pass, transparent_pass](uint32_t, RHIRenderPass* pass) {
            // DepthPrepass runs to clear depth; voxels draw in Opaque with LE depth.
            return pass == depth_prepass || pass == opaque_pass || pass == transparent_pass;
        });
    renderer.pass_group(PassGroupId::Skybox).add_render_pass(skybox_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "Voxel Perlin Scene";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);

    imgui_renderer.set_frame_callback([&]() {
        Widgets& widgets = *window->widgets();
        if (renderer.is_async_loading()) {
            display_loading_progress(widgets, &loading_progress, renderer.dt());
        } else {
            display_frame_info(widgets);
            widgets.use_window("Voxel Scene", [&]() {
                widgets.text(ocarina::format(
                    "World: {}x{}x{} (3D Perlin FBM → noise texture → VoxelWorldData)",
                    kWorldSize,
                    kWorldSize,
                    kWorldSize));
                widgets.text(ocarina::format(
                    "Solid voxels: {}  Chunks: {}",
                    voxel_world.solid_voxel_count(),
                    voxel_world.chunk_count()));
            });
        }
    });

    renderer.set_async_loader(&scene_loader, nullptr, [&]() {
        renderer.set_skybox_material(skybox_material);
        if (scene != nullptr) {
            renderer.set_scene(scene);
            frame_camera_to_bounds(camera, voxel_world.world_bounds_meters());
        }
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
