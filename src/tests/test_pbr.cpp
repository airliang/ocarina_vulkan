//
// IBL PBR test:
// 1. Bake Yokohama2 env cubemap, Lambertian irradiance, GGX specular mips, BRDF LUT.
// 2. Skybox samples the original env cubemap.
// 3. FlightHelmet is shaded with mesh.vert + mesh_pbr.frag (split-sum IBL).
//

#include "core/stl.h"
#include "core/logging.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "rhi/resources/cubemap.h"
#include "rhi/resources/texture.h"
#include "rhi/bindless_sampler.h"
#include "rhi/resources/texture_sampler.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/widgets.h"
#include "framework/renderer.h"
#include "framework/scene.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "rhi/shader_parameters.h"
#include "framework/async_loader.h"
#include "framework/gltf_async_loader.h"
#include "framework/loading_progress_listener.h"
#include "framework/pso_request.h"
#include "framework/frame_resources.h"
#include "framework/pass_group_id.h"
#include "framework/bindless_texture_registry.h"
#include "framework/render_component.h"
#include "rhi/shader_program.h"
#include "rhi/shader_program_key.h"
#include "rhi/pipeline_state.h"
#include "core/image.h"

#include "ext/enkiTS/src/TaskScheduler.h"

#include <algorithm>

using namespace ocarina;

namespace {

constexpr uint32_t kIrradianceSize = 64;
constexpr uint32_t kSpecularSize = 128;
constexpr uint32_t kBrdfLutSize = 512;

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

void bind_ibl_to_scene(
    Scene& scene,
    Cubemap* irradiance,
    Cubemap* specular,
    const TextureSampler& sampler) {
    if (irradiance == nullptr || specular == nullptr) {
        return;
    }
    for (uint32_t index = 0; index < scene.primitive_count(); ++index) {
        Material* material = scene.render_component(index).get_material();
        if (material == nullptr) {
            continue;
        }
        material->set_cubemap("irradianceMap", irradiance);
        material->add_sampler("sampler_irradianceMap", sampler);
        material->set_cubemap("specularMap", specular);
        material->add_sampler("sampler_specularMap", sampler);
    }
}

class CombinedLoader : public enki::ITaskSet {
public:
    AsyncLoader* ibl_loader = nullptr;
    GltfAsyncLoader* gltf_loader = nullptr;

    CombinedLoader() {
        m_SetSize = 1;
        m_MinRange = 1;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
        if (ibl_loader != nullptr) {
            ibl_loader->ExecuteRange(range, threadnum);
        }
        if (gltf_loader != nullptr) {
            gltf_loader->ExecuteRange(range, threadnum);
        }
    }
};

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();
    file_manager.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("PBR IBL", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Renderer renderer(&device);
    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();

    const fs::path pipeline_path = project_root / "res/pipelines/forward_pbr.pipeline.json";
    RenderTarget swapchain_target = RenderTarget::swapchain();
    // Default forward path: DepthPrepass (clear/write depth) → Opaque (Load depth, Equal) → Transparent.
    // All three passes share swapchain_target so Opaque reuses the DepthPrepass depth buffer.
    if (!renderer.load_default_pipeline(&swapchain_target, pipeline_path)) {
        OC_ERROR("Failed to load default ForwardPBR pipeline (DepthPrepass/Opaque/Transparent)");
        return -1;
    }

    ShaderProgram* irradiance_program = nullptr;
    ShaderProgram* specular_program = nullptr;
    ShaderProgram* brdf_lut_program = nullptr;
    std::unique_ptr<ShaderParameters> irradiance_params;
    std::unique_ptr<ShaderParameters> brdf_lut_params;
    ocarina_vector<std::unique_ptr<ShaderParameters>> specular_mip_params;
    Material* skybox_material = nullptr;
    Cubemap* env_cubemap = nullptr;
    Cubemap* irradiance_cubemap = nullptr;
    Cubemap* specular_cubemap = nullptr;
    Texture* brdf_lut = nullptr;
    bool ibl_baked = false;
    float env_face_resolution = 1.0f;
    TextureSampler ibl_sampler{
        TextureSampler::Filter::LINEAR_LINEAR,
        TextureSampler::Address::EDGE};

    
    const fs::path shader_vert = project_root / "res/shaderlibrary/builtin/fullscreen.vert";
    const fs::path shader_frag = project_root / "res/shaderlibrary/builtin/skybox.frag";
    const fs::path mesh_vert = project_root / "res/shaderlibrary/builtin/mesh.vert";
    const fs::path mesh_pbr_frag = project_root / "res/shaderlibrary/builtin/mesh_pbr.frag";
    const fs::path irradiance_path = project_root / "res/shaderlibrary/builtin/irradiance_convolution.compute";
    const fs::path specular_path = project_root / "res/shaderlibrary/builtin/specular_prefilter.compute";
    const fs::path brdf_lut_path = project_root / "res/shaderlibrary/builtin/brdf_lut.compute";
    const fs::path cubemap_dir = project_root / "res/textures/Yokohama2";
    const fs::path gltf_path = project_root / "res/FlightHelmet/glTF/FlightHelmet.gltf";
    //const fs::path gltf_path = project_root / "res/jabami_anime_tree_v3/scene.gltf";

    const ocarina_string shader_vert_abs = ocarina::to_ocarina_string(fs::absolute(shader_vert).string());
    const ocarina_string shader_frag_abs = ocarina::to_ocarina_string(fs::absolute(shader_frag).string());
    const ocarina_string mesh_vert_abs = ocarina::to_ocarina_string(fs::absolute(mesh_vert).string());
    const ocarina_string mesh_pbr_frag_abs = ocarina::to_ocarina_string(fs::absolute(mesh_pbr_frag).string());
    const ocarina_string irradiance_abs = ocarina::to_ocarina_string(fs::absolute(irradiance_path).string());
    const ocarina_string specular_abs = ocarina::to_ocarina_string(fs::absolute(specular_path).string());
    const ocarina_string brdf_lut_abs = ocarina::to_ocarina_string(fs::absolute(brdf_lut_path).string());

    LoadingProgressListener loading_progress;
    renderer.set_loading_progress_listener(&loading_progress);

    AsyncLoader ibl_loader(
        &renderer.task_scheduler(),
        &device,
        [&](Device* load_device) {
            ocarina_set<string> options;
            ResourceManager& resources = ResourceManager::instance();

            irradiance_program = resources.create_compute_shader_program(
                load_device, irradiance_abs, options);
            irradiance_params = std::make_unique<ShaderParameters>(load_device, irradiance_program);

            specular_program = resources.create_compute_shader_program(
                load_device, specular_abs, options);

            brdf_lut_program = resources.create_compute_shader_program(
                load_device, brdf_lut_abs, options);
            brdf_lut_params = std::make_unique<ShaderParameters>(load_device, brdf_lut_program);

            ShaderProgram* skybox_program = resources.create_shader_program(
                load_device, shader_vert_abs, shader_frag_abs, options, options);
            skybox_material = resources.create_material(load_device, skybox_program);
            skybox_material->set_depth_stencil_state(PassGroupId::Opaque, make_skybox_depth_state());
            skybox_material->set_raster_state(PassGroupId::Opaque, make_skybox_raster_state());

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

            env_face_resolution = static_cast<float>(faces[0].resolution().x);
            env_cubemap = resources.create_empty_cubemap(
                load_device,
                faces[0].resolution().x,
                faces[0].resolution().y,
                faces[0].pixel_storage(),
                ibl_sampler,
                cubemap_sampled_upload_usage(),
                cubemap_mip_count(faces[0].resolution().x, faces[0].resolution().y));
            resources.upload_cubemap_faces(load_device, env_cubemap, faces);

            irradiance_cubemap = resources.create_empty_cubemap(
                load_device,
                kIrradianceSize,
                kIrradianceSize,
                PixelStorage::BYTE4,
                ibl_sampler,
                cubemap_storage_usage());

            const uint32_t specular_mips = cubemap_mip_count(kSpecularSize, kSpecularSize);
            specular_cubemap = resources.create_empty_cubemap(
                load_device,
                kSpecularSize,
                kSpecularSize,
                PixelStorage::BYTE4,
                ibl_sampler,
                cubemap_storage_usage(),
                specular_mips);

            specular_mip_params.resize(specular_mips);
            for (uint32_t mip = 0; mip < specular_mips; ++mip) {
                specular_mip_params[mip] = std::make_unique<ShaderParameters>(
                    load_device, specular_program);
            }

            const TextureUsageFlags brdf_lut_usage = static_cast<TextureUsageFlags>(
                static_cast<uint32_t>(TextureUsageFlags::ShaderReadWrite)
                | static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly));
            brdf_lut = resources.create_render_target_texture(
                load_device,
                "brdf_lut",
                kBrdfLutSize,
                kBrdfLutSize,
                PixelStorage::HALF4,
                brdf_lut_usage);

            skybox_material->set_cubemap("skybox", env_cubemap);
            skybox_material->add_sampler("sampler_skybox", ibl_sampler);
        });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    camera.set_position({0.0f, 0.15f, -0.45f});
    camera.set_target({0.0f, 0.12f, 0.0f});
    camera.set_move_speed(0.2f);

    // LDR IBL is weak vs a hot sun; keep direct light modest so specular IBL reads.
    FrameResources::instance().set_sun_intensity(1.5f);

    RenderPassCreation compute_pass_creation;
    compute_pass_creation.render_target = nullptr;
    RHIRenderPass* compute_pass = device.create_render_pass(compute_pass_creation);
    compute_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (ibl_baked) {
            return;
        }
        if (irradiance_program == nullptr || irradiance_params == nullptr
            || specular_program == nullptr || brdf_lut_program == nullptr
            || brdf_lut_params == nullptr || env_cubemap == nullptr
            || irradiance_cubemap == nullptr || specular_cubemap == nullptr
            || brdf_lut == nullptr) {
            return;
        }
        if (!env_cubemap->is_gpu_ready() || !brdf_lut->is_gpu_ready()
            || !irradiance_params->is_ready() || !brdf_lut_params->is_ready()) {
            return;
        }
        for (const auto& mip_params : specular_mip_params) {
            if (mip_params == nullptr || !mip_params->is_ready()) {
                return;
            }
        }

        irradiance_params->set_cubemap("envMap", env_cubemap);
        irradiance_params->set_cubemap("irradianceMap", irradiance_cubemap);

        const handle_ty irradiance_handle = reinterpret_cast<handle_ty>(irradiance_cubemap->impl());
        cmd.transition_cubemap_layout(
            irradiance_handle,
            TextureLayout::Undefined,
            TextureLayout::General);

        renderer.dispatch_compute_shader_for_extent(
            cmd, *irradiance_params, kIrradianceSize, kIrradianceSize, 6);

        cmd.transition_cubemap_layout(
            irradiance_handle,
            TextureLayout::General,
            TextureLayout::ShaderReadOnly);

        const uint32_t specular_mips = specular_cubemap->mip_levels();
        const handle_ty specular_handle = reinterpret_cast<handle_ty>(specular_cubemap->impl());
        cmd.transition_cubemap_layout(
            specular_handle,
            TextureLayout::Undefined,
            TextureLayout::General);

        for (uint32_t mip = 0; mip < specular_mips; ++mip) {
            ShaderParameters* mip_params = specular_mip_params[mip].get();
            const float roughness = specular_mips > 1
                                        ? static_cast<float>(mip) / static_cast<float>(specular_mips - 1)
                                        : 0.0f;
            mip_params->set_cubemap("envMap", env_cubemap);
            mip_params->set_cubemap("prefilteredMap", specular_cubemap, mip);
            mip_params->set_float("roughness", roughness);
            mip_params->set_float("envResolution", env_face_resolution);

            const uint32_t mip_size = std::max(kSpecularSize >> mip, 1u);
            renderer.dispatch_compute_shader_for_extent(cmd, *mip_params, mip_size, mip_size, 6);
        }

        cmd.transition_cubemap_layout(
            specular_handle,
            TextureLayout::General,
            TextureLayout::ShaderReadOnly);

        brdf_lut_params->set_texture("brdfLut", brdf_lut);
        const handle_ty brdf_lut_handle = reinterpret_cast<handle_ty>(brdf_lut->impl());
        cmd.transition_texture_layout(
            brdf_lut_handle,
            TextureLayout::Undefined,
            TextureLayout::General);
        renderer.dispatch_compute_shader_for_extent(
            cmd, *brdf_lut_params, kBrdfLutSize, kBrdfLutSize);
        cmd.transition_texture_layout(
            brdf_lut_handle,
            TextureLayout::General,
            TextureLayout::ShaderReadOnly);

        const uint32_t lut_index = BindlessTextureRegistry::instance().get_index(brdf_lut);
        FrameResources::instance().set_brdf_lut_index(lut_index);
        FrameResources::instance().set_brdf_lut_sampler_index(
            get_bindless_sampler_index(
                TextureSampler{
                    TextureSampler::Filter::LINEAR_LINEAR,
                    TextureSampler::Address::CLAMP}));

        ibl_baked = true;
        renderer.set_skybox_material(skybox_material);
        if (Scene* scene = renderer.scene()) {
            bind_ibl_to_scene(*scene, irradiance_cubemap, specular_cubemap, ibl_sampler);
        }
    });

    // Skybox + UI stay hard-coded; Opaque / Transparent come from the pipeline JSON.
    RHIRenderPass* depth_prepass = renderer.find_pipeline_render_pass("DepthPrepass");
    RHIRenderPass* opaque_pass = renderer.find_pipeline_render_pass("Opaque");
    RHIRenderPass* transparent_pass = renderer.find_pipeline_render_pass("Transparent");
    if (depth_prepass == nullptr || opaque_pass == nullptr || transparent_pass == nullptr) {
        OC_ERROR("Default pipeline missing DepthPrepass / Opaque / Transparent RHIRenderPass");
        return -1;
    }

    // DepthPrepass (pipeline) clears depth; skybox only clears color.
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
    ui_pass_creation.present_swapchain = true;
    RHIRenderPass* ui_pass = device.create_render_pass(ui_pass_creation);

    PSORequest skybox_pso = PSORequest::make_graphics(shader_vert_abs, shader_frag_abs, skybox_pass);
    skybox_pso.raster_state = make_skybox_raster_state();
    skybox_pso.depth_stencil_state = make_skybox_depth_state();
    ibl_loader.set_pso_requests({std::move(skybox_pso)});
    ibl_loader.set_target_render_pass(skybox_pass);

    // Gltf loads after pipeline asset: materials are built from MaterialAsset + pipeline passes.
    GltfAsyncLoader gltf_loader(
        &renderer.task_scheduler(),
        &device,
        &renderer,
        mesh_vert_abs,
        mesh_pbr_frag_abs,
        ocarina::to_ocarina_string(fs::absolute(gltf_path).string()));
    gltf_loader.set_progress_listener(&loading_progress);

    CombinedLoader combined_loader;
    combined_loader.ibl_loader = &ibl_loader;
    combined_loader.gltf_loader = &gltf_loader;

    renderer.set_camera(&camera);
    // Scene geometry: DepthPrepass / Opaque / Transparent from MaterialAsset pass_participation.
    renderer.set_render_pass_primitive_filter(
        [depth_prepass, opaque_pass, transparent_pass](uint32_t, RHIRenderPass* pass) {
            return pass == depth_prepass || pass == opaque_pass || pass == transparent_pass;
        });
    renderer.pass_group(PassGroupId::ComputePass).add_render_pass(compute_pass);
    renderer.pass_group(PassGroupId::Skybox).add_render_pass(skybox_pass);
    // DepthPrepass / Opaque / Transparent already registered by load_pipeline.
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "PBR IBL";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);

    auto on_gltf_loaded = [&]() {
        Scene& scene = gltf_loader.get_scene();
        bind_ibl_to_scene(scene, irradiance_cubemap, specular_cubemap, ibl_sampler);
        renderer.set_scene(&scene);
        renderer.set_skybox_material(skybox_material);
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

        // Drop the live scene while the loader window runs, then rebuild from the new file.
        renderer.set_scene(nullptr);
        loading_progress.reset();
        gltf_loader.reset_for_reload(
            ocarina::to_ocarina_string(fs::absolute(selected).string()));
        gltf_loader.set_progress_listener(&loading_progress);

        renderer.set_async_loader(&gltf_loader, nullptr, on_gltf_loaded);
        renderer.start_async_loader();
    };

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
    });

    renderer.set_async_loader(&combined_loader, nullptr, on_gltf_loaded);

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
