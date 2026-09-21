//
// IBL PBR test (Phase 2 + 3):
// 1. Empty env cubemap, bake Yokohama2 six faces (with mips for PDF lod).
// 2. Irradiance cubemap: one-shot Lambertian convolution.
// 3. Specular prefilter cubemap: one-shot GGX convolution per roughness mip.
// 4. Skybox samples the prefiltered specular cubemap (mip 0).
//

#include "core/stl.h"
#include "math/basic_types.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "rhi/resources/cubemap.h"
#include "framework/window_factory.h"
#include "framework/sdl_window.h"
#include "framework/imgui_renderer.h"
#include "framework/framework_ui.h"
#include "framework/renderer.h"
#include "framework/scene.h"
#include "rhi/renderpass.h"
#include "rhi/rendertarget.h"
#include "framework/camera.h"
#include "framework/resource_manager.h"
#include "framework/material.h"
#include "rhi/shader_parameters.h"
#include "framework/async_loader.h"
#include "framework/pso_request.h"
#include "framework/frame_resources.h"
#include "framework/pass_group_id.h"
#include "rhi/shader_program.h"
#include "core/image.h"

#include <algorithm>

using namespace ocarina;

namespace {

constexpr uint32_t kIrradianceSize = 64;
constexpr uint32_t kSpecularSize = 128;

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

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext& file_manager = RHIContext::instance();
    file_manager.parse_command_line(argc, argv);

    const uint2 window_size = make_uint2(1280, 720);
    auto window = create_sdl_window("PBR IBL Specular Prefilter", window_size);

    InstanceCreation instanceCreation = {};
    instanceCreation.windowHandle = window->get_window_handle();
    instanceCreation.windowWidth = window_size.x;
    instanceCreation.windowHeight = window_size.y;
    Device device = file_manager.create_device("vulkan", instanceCreation);

    Scene scene;
    Renderer renderer(&device);

    ShaderProgram* irradiance_program = nullptr;
    ShaderProgram* specular_program = nullptr;
    std::unique_ptr<ShaderParameters> irradiance_params;
    std::vector<std::unique_ptr<ShaderParameters>> specular_mip_params;
    Material* skybox_material = nullptr;
    Cubemap* env_cubemap = nullptr;
    Cubemap* irradiance_cubemap = nullptr;
    Cubemap* specular_cubemap = nullptr;
    bool ibl_baked = false;
    float env_face_resolution = 1.0f;

    const fs::path source_dir = fs::path(__FILE__).parent_path();
    const fs::path project_root = source_dir.parent_path().parent_path();
    const fs::path shader_vert = project_root / "res/shaderlibrary/builtin/fullscreen.vert";
    const fs::path shader_frag = project_root / "res/shaderlibrary/builtin/skybox.frag";
    const fs::path irradiance_path = project_root / "res/shaderlibrary/builtin/irradiance_convolution.compute";
    const fs::path specular_path = project_root / "res/shaderlibrary/builtin/specular_prefilter.compute";
    const fs::path cubemap_dir = project_root / "res/textures/Yokohama2";

    const std::string shader_vert_abs = fs::absolute(shader_vert).string();
    const std::string shader_frag_abs = fs::absolute(shader_frag).string();
    const std::string irradiance_abs = fs::absolute(irradiance_path).string();
    const std::string specular_abs = fs::absolute(specular_path).string();

    AsyncLoader async_loader(
        &renderer.task_scheduler(),
        &device,
        [&](Device* load_device) {
            std::set<string> options;
            ResourceManager& resources = ResourceManager::instance();

            irradiance_program = resources.create_compute_shader_program(
                load_device, irradiance_abs, options);
            irradiance_params = std::make_unique<ShaderParameters>(load_device, irradiance_program);

            specular_program = resources.create_compute_shader_program(
                load_device, specular_abs, options);

            ShaderProgram* skybox_program = resources.create_shader_program(
                load_device, shader_vert_abs, shader_frag_abs, options, options);
            skybox_material = resources.create_material(load_device, skybox_program);
            skybox_material->set_depth_stencil_state(make_skybox_depth_state());
            skybox_material->set_raster_state(make_skybox_raster_state());

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

            TextureSampler sampler{
                TextureSampler::Filter::LINEAR_LINEAR,
                TextureSampler::Address::EDGE};

            env_face_resolution = static_cast<float>(faces[0].resolution().x);
            env_cubemap = resources.create_empty_cubemap(
                load_device,
                faces[0].resolution().x,
                faces[0].resolution().y,
                faces[0].pixel_storage(),
                sampler,
                cubemap_sampled_upload_usage(),
                cubemap_mip_count(faces[0].resolution().x, faces[0].resolution().y));
            resources.upload_cubemap_faces(load_device, env_cubemap, faces);

            irradiance_cubemap = resources.create_empty_cubemap(
                load_device,
                kIrradianceSize,
                kIrradianceSize,
                PixelStorage::BYTE4,
                sampler,
                cubemap_storage_usage());

            const uint32_t specular_mips = cubemap_mip_count(kSpecularSize, kSpecularSize);
            specular_cubemap = resources.create_empty_cubemap(
                load_device,
                kSpecularSize,
                kSpecularSize,
                PixelStorage::BYTE4,
                sampler,
                cubemap_storage_usage(),
                specular_mips);

            specular_mip_params.resize(specular_mips);
            for (uint32_t mip = 0; mip < specular_mips; ++mip) {
                specular_mip_params[mip] = std::make_unique<ShaderParameters>(
                    load_device, specular_program);
            }

            skybox_material->set_cubemap("skybox", specular_cubemap);
            skybox_material->add_sampler("sampler_skybox", sampler);
        });

    Camera camera;
    window->add_event_listener(&camera);
    camera.set_aspect_ratio(static_cast<float>(window_size.x) / static_cast<float>(window_size.y));
    camera.set_position({0.0f, 0.0f, 0.0f});
    camera.set_target({0.0f, 0.0f, 1.0f});

    RenderPassCreation compute_pass_creation;
    compute_pass_creation.render_target = nullptr;
    RHIRenderPass* compute_pass = device.create_render_pass(compute_pass_creation);
    compute_pass->set_execute_callback([&](CommandBuffer& cmd) {
        if (ibl_baked) {
            return;
        }
        if (irradiance_program == nullptr || irradiance_params == nullptr
            || specular_program == nullptr || env_cubemap == nullptr
            || irradiance_cubemap == nullptr || specular_cubemap == nullptr) {
            return;
        }
        if (!env_cubemap->is_gpu_ready() || !irradiance_params->is_ready()) {
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

        ibl_baked = true;
        renderer.set_skybox_material(skybox_material);
    });

    RenderTarget swapchain_target = RenderTarget::swapchain();

    RenderPassCreation skybox_pass_creation;
    skybox_pass_creation.render_target = &swapchain_target;
    skybox_pass_creation.clear_color = make_float4(0.02f, 0.02f, 0.02f, 1.0f);
    skybox_pass_creation.clear_depth = 1.0f;
    skybox_pass_creation.clear_stencil = 0;
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
    async_loader.set_pso_requests({std::move(skybox_pso)});

    renderer.set_scene(&scene);
    renderer.set_camera(&camera);
    renderer.set_frustum_culling_enabled(false);
    renderer.pass_group(PassGroupId::ComputePass).add_render_pass(compute_pass);
    renderer.pass_group(PassGroupId::Skybox).add_render_pass(skybox_pass);
    renderer.pass_group(PassGroupId::UI).add_render_pass(ui_pass);

    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    const string window_name = "PBR IBL Specular Prefilter";
    FrameInfoContext frame_info;
    frame_info.renderer = &renderer;
    frame_info.device = &device;
    frame_info.window_title = window_name;
    window->widgets()->set_frame_info_context(&frame_info);
    imgui_renderer.set_frame_callback([&]() {
        display_loading_progress(*window->widgets(), nullptr, renderer.dt());
    });

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
        imgui_renderer.cleanup();
        window->remove_event_listener(&camera);
    });

    renderer.run();
    window->run([](double) {});
    renderer.shutdown();
}
