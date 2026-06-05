//
// Created by Zero on 2022/8/16.
//


#include "core/stl.h"
#include "dsl/dsl.h"
//#include "util/file_manager.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <windows.h>
#include "math/base.h"
//#include "util/image.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
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
#include "rhi/fence.h"
#include "framework/frame_resources.h"

using namespace ocarina;


struct GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix;
    math3d::Matrix4 view_matrix;
};

struct PushConstants {
    float4x4 world_matrix;
    uint32_t albedo_index;
};


int main(int argc, char *argv[]) {

    fs::path path(argv[0]);
    //FileManager &file_manager = FileManager::instance();
    RHIContext &file_manager = RHIContext::instance();

    auto window = create_sdl_window("display", make_uint2(800, 600));

    InstanceCreation instanceCreation = {};
    //instanceCreation.instanceExtentions =
    instanceCreation.windowHandle = window->get_window_handle();
    Device device = file_manager.create_device("vulkan", instanceCreation);
    Primitive quad;
    Material* material = nullptr;
    Mesh* quad_mesh = nullptr;
    Texture* texture = nullptr;
    AsyncLoader async_loader(&device, [&material, &quad_mesh, &texture](Device* device) {
        // Your async task code here
        //Shader
        std::set<string> options;
        handle_ty vertex_shader = device->create_shader_from_file("D:\\github\\Vision\\src\\ocarina\\src\\backends\\vulkan\\builtin\\texture.vert", ShaderType::VertexShader, options);
        handle_ty pixel_shader = device->create_shader_from_file("D:\\github\\Vision\\src\\ocarina\\src\\backends\\vulkan\\builtin\\bindless_texture.frag", ShaderType::PixelShader, options);

        material = ResourceManager::instance().create_material(device, vertex_shader, pixel_shader);

        Image image = Image::load("D:\\Github\\Vision\\src\\ocarina\\res\\textures\\granite.png", ColorSpace::SRGB);
        TextureViewCreation texture_view = {};
        texture_view.mip_level_count = 0;
        texture_view.usage = TextureUsageFlags::ShaderReadOnly;
        TextureSampler sampler{ TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT };
        texture = ResourceManager::instance().create_texture(device, image, texture_view, sampler);//device.create_texture(&image, texture_view, sampler);

        uint64_t albedo_name_id = hash64("albedo");

        quad_mesh = ResourceManager::instance().create_mesh(device, "quad");
        
    });

    auto setup_quad = [&](Primitive& quad) {
        //quad.set_vertex_shader(vertex_shader);
        //quad.set_pixel_shader(pixel_shader);
        
        quad.set_mesh(quad_mesh);
        //pipeline_state.vertex_buffer = quad.get_mesh()->vertex_buffer();
        //quad.set_pipeline_state(pipeline_state);
        quad.set_material(material);

        quad.set_mesh(quad_mesh);
        //pipeline_state.vertex_buffer = quad.get_mesh()->vertex_buffer();
        //quad.set_pipeline_state(pipeline_state);
        quad.set_material(material);
        uint64_t albedo_name_id = hash64("albedo");
        quad.add_bindless_texture(albedo_name_id, texture);
        uint64_t name_id = hash64("sampler_albedo");
        quad.add_sampler(name_id, *(texture->get_sampler_pointer()));
    };

    Camera camera;
    camera.set_aspect_ratio(800.0f / 600.0f);
    camera.set_position({0.0f, 0.0f, -2.5f});
    camera.set_target({0.0f, 0.0f, 0.0f});

    uint64_t push_constant_name_id = hash64("PushConstants");

    auto pre_render_draw_item = [&](const DrawCallItem &item) {
        //update push constants before draw
        //item.descriptor_set_writer->update_push_constants(push_constant_name_id, (void *)&item.world_matrix, sizeof(item.world_matrix), item.pipeline_line);
    };

    PushConstants push_constants_data = {};

    uint64_t name_id_world_matrix = hash64("modelMatrix");
    uint64_t name_id_albedo_index = hash64("albedoIndex");
    uint64_t albedo_name_id = hash64("albedo");
    auto update_push_constant = [&](Primitive &primitive) {
        // Setup push constant data if needed
        push_constants_data.world_matrix = primitive.get_world_matrix();
        primitive.set_push_constant_variable(
            name_id_world_matrix,
            reinterpret_cast<std::byte *>(const_cast<void *>(static_cast<const void *>(&primitive.get_world_matrix()))),
            sizeof(primitive.get_world_matrix()));
        Primitive::TextureHandle albedo_texture_handle = primitive.get_texture_handle(albedo_name_id);
        push_constants_data.albedo_index = albedo_texture_handle.bindless_index_;
        //primitive.set_push_constant_data(reinterpret_cast<const std::byte *>(&push_constants_data));
        primitive.set_push_constant_variable(
            name_id_albedo_index,
            reinterpret_cast<std::byte *>(&albedo_texture_handle.bindless_index_),
            sizeof(push_constants_data.albedo_index));
    };

    quad.set_draw_call_pre_render_function(pre_render_draw_item);
    quad.set_update_push_constant_function(update_push_constant);

    RenderPassCreation render_pass_creation;
    render_pass_creation.swapchain_clear_color = make_float4(0.1f, 0.1f, 0.1f, 1.0f);
    render_pass_creation.swapchain_clear_depth = 1.0f;
    render_pass_creation.swapchain_clear_stencil = 0;
    RHIRenderPass *render_pass = device.create_render_pass(render_pass_creation);

    auto setup_quad_with_pipeline = [&](Primitive& quad) {
        setup_quad(quad);
        if (material != nullptr) {
            material->set_target_render_pass(render_pass);
        }
    };

    Renderer renderer(&device);
    renderer.set_async_loader(&async_loader, nullptr, [&]() {
        quad.set_geometry_data_setup(&device, setup_quad_with_pipeline);
    });

    //DescriptorSet *global_descriptor_set = device.get_global_descriptor_set("global_ubo");
    FrameResources::instance().set_update_callback([&](FrameResources&, double) {
        DescriptorSet* global_descriptor_set = FrameResources::instance().get_global_descriptor_set("global_ubo");
        GlobalUniformBuffer global_ubo_data = {camera.get_projection_matrix().transpose(), camera.get_view_matrix().transpose()};
        global_descriptor_set->update_buffer(hash64("global_ubo"), &global_ubo_data, sizeof(GlobalUniformBuffer));
        render_pass->clear_draw_call_items();
        auto draw_item = quad.get_draw_call_item(&device, render_pass);
        render_pass->add_draw_call(draw_item);
    });


    
    renderer.add_render_pass(render_pass);

    auto image_io = Image::pure_color(make_float4(1, 0, 0, 1), ColorSpace::LINEAR, make_uint2(500));
    window->set_background(image_io.pixel_ptr<float4>(), make_uint2(800, 600));
    ImguiRenderer imgui_renderer(*window);
    imgui_renderer.init(device);
    string window_name = "Vulkan Bindless Texture Test";
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
    });

    renderer.run();
    window->run([](double) {});
    renderer.shutdown();
}