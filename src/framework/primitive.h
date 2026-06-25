//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_pool.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "render_component.h"
#include "rhi/resources/texture.h"

namespace ocarina {
class VertexBuffer;
class IndexBuffer;
class DescriptorSet;
class DescriptorSetLayout;
class Device;
class DescriptorSetWriter;
struct RHIPipeline;
class TextureSampler;
class Material;
class Mesh;
class TransformComponent;


class Primitive {

public:
    Primitive() {}
    ~Primitive();

    using GeometryDataSetup = ocarina::function<void(Primitive&)>;
    using UpdatePushConstant = ocarina::function<void(Primitive&, TransformComponent&)>;

    void set_geometry_data_setup(Device *device, GeometryDataSetup setup);

    void set_update_push_constant_function(UpdatePushConstant func) {
        update_push_constant_function_ = func;
    }   

    struct TextureHandle {
        uint32_t bindless_index_ = InvalidUI32;
        Texture *texture_ = nullptr;
    };

    TextureHandle get_texture_handle(uint64_t name_id) const;

    void add_descriptor_set(DescriptorSet* descriptor_set);

    void update_render_component(Device* device, RenderComponent& render_component, TransformComponent& transform);

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
            material_parameters_buffer_.clear();
            material_parameters_dirty_ = true;
        }
    }

    Material* get_material() const {
        return material_;
    }

    void set_material_parameter(uint64_t name_id, const void* data, size_t size);
    void set_material_parameter(const char* name, const void* data, size_t size) {
        set_material_parameter(hash64(name), data, size);
    }

    template<typename T>
    void set_material_parameter(uint64_t name_id, const T& value) {
        set_material_parameter(name_id, &value, sizeof(T));
    }

    template<typename T>
    void set_material_parameter(const char* name, const T& value) {
        set_material_parameter(hash64(name), value);
    }
private:
    void update_descriptor_sets(Device *device);
    void upload_material_parameters();

    GeometryDataSetup geometry_data_setup_;
    UpdatePushConstant update_push_constant_function_ = nullptr;

    std::byte *push_constant_data_ = nullptr;
    
    std::unordered_map<uint64_t, TextureHandle> textures_;
    std::unordered_map<uint64_t, TextureHandle> bindless_textures_;
    std::unordered_map<uint64_t, TextureSampler> samplers_;
    std::vector<DescriptorSet *> descriptor_sets_;
    bool descriptor_sets_dirty_ = true;
    Material* descriptor_sets_material_ = nullptr;
    uint32_t first_descriptor_set_ = 0;

    Material* material_ = nullptr;
    Mesh* mesh_ = nullptr;

    std::vector<uint8_t> material_parameters_buffer_;
    bool material_parameters_dirty_ = true;
};

}// namespace ocarina
