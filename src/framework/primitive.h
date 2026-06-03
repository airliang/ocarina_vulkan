//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_pool.h"
#include "rhi/params.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/renderpass.h"
#include "rhi/resources/texture.h"
#include "math/transform.h"

namespace ocarina {
class VertexBuffer;
class IndexBuffer;
template <class T>
class Shader;
class DescriptorSet;
class DescriptorSetLayout;
class Device;
class DescriptorSetWriter;
struct RHIPipeline;
class TextureSampler;
class Material;
class Mesh;


class Primitive {

public:
    Primitive() {}
    ~Primitive();

    using GeometryDataSetup = ocarina::function<void(Primitive&)>;
    using UpdatePushConstant = ocarina::function<void(Primitive&)>;

    void set_geometry_data_setup(Device *device, GeometryDataSetup setup);
    void set_draw_call_pre_render_function(DrawCallItem::PreRenderFunction pre_render_function) {
        drawcall_pre_draw_function_ = pre_render_function;
    }

    void set_update_push_constant_function(UpdatePushConstant func) {
        update_push_constant_function_ = func;
    }   

    struct TextureHandle {
        uint32_t bindless_index_ = InvalidUI32;
        Texture *texture_ = nullptr;
    };

    TextureHandle get_texture_handle(uint64_t name_id) const;

    void add_descriptor_set(DescriptorSet* descriptor_set);

    void set_position(const float3 &position) {
        position_ = position;
        transform_.set_position(position);
        transform_dirty_ = true;
    }

    const float3 &get_position() const { return position_; }

    const float4x4& get_world_matrix() {
        if (transform_dirty_) {
            transform_.set_TRS(position_, rotation_, float3(1, 1, 1));
            transform_dirty_ = false;
            world_matrix_ = transform_.mat4x4();
        }
        return world_matrix_;
    }

    const Transform<float4x4>& get_transform() const {
        return transform_;
    }

    DrawCallItem get_draw_call_item(Device *device, RHIRenderPass *render_pass);

    void add_texture(uint64_t name_id, Texture* texture);

    void add_bindless_texture(uint64_t name_id, Texture *texture);

    void add_sampler(uint64_t name_id, const TextureSampler& sampler);

    void set_push_constant_variable(uint64_t name_id, const std::byte *data, size_t size);

    void set_mesh(Mesh* mesh) {
        mesh_ = mesh;
    }

    Mesh* get_mesh() const {
        return mesh_;
    }

    void set_material(Material* material) {
        if (material_ != material) {
            material_ = material;
            descriptor_sets_dirty_ = true;
        }
    }

    Material* get_material() const {
        return material_;
    }
private:
    void update_descriptor_sets(Device *device);

    GeometryDataSetup geometry_data_setup_;
    DrawCallItem::PreRenderFunction drawcall_pre_draw_function_ = nullptr;
    UpdatePushConstant update_push_constant_function_ = nullptr;

    float4x4 world_matrix_;
    float3 position_;
    quaternion rotation_ = quaternion(0, 0, 0, 1);
    bool transform_dirty_ = true;
    std::byte *push_constant_data_ = nullptr;
    
    std::unordered_map<uint64_t, TextureHandle> textures_;
    std::unordered_map<uint64_t, TextureHandle> bindless_textures_;
    std::unordered_map<uint64_t, TextureSampler> samplers_;
    std::vector<DescriptorSet *> descriptor_sets_;
    bool descriptor_sets_dirty_ = true;
    Material* descriptor_sets_material_ = nullptr;

    DrawCallItem item_;

    Material* material_ = nullptr;
    Mesh* mesh_ = nullptr;
    Transform<float4x4> transform_;
};

}// namespace ocarina