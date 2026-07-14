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

namespace ocarina {
class VertexBuffer;
class IndexBuffer;
class Device;
struct RHIPipeline;
class Material;
class Mesh;
class TransformComponent;

class Primitive {
public:
    Primitive() = default;
    Primitive(Primitive&&) noexcept = default;
    Primitive& operator=(Primitive&&) noexcept = default;
    Primitive(const Primitive&) = delete;
    Primitive& operator=(const Primitive&) = delete;
    ~Primitive();

    using GeometryDataSetup = ocarina::function<void(Primitive&)>;
    using UpdatePushConstant = ocarina::function<void(Primitive&, TransformComponent&)>;

    void set_entity_index(uint32_t entity_index) noexcept { entity_index_ = entity_index; }
    [[nodiscard]] uint32_t entity_index() const noexcept { return entity_index_; }

    void set_geometry_data_setup(Device* device, GeometryDataSetup setup);
    void set_update_push_constant_function(UpdatePushConstant func) {
        update_push_constant_function_ = func;
    }

    void initialize_render_component(
        Device* device,
        RenderComponent& render_component,
        TransformComponent& transform);
    void update_push_constants(TransformComponent& transform);
    void update_render_component(Device* device, RenderComponent& render_component, TransformComponent& transform);
    void set_push_constant_variable(uint64_t name_id, const std::byte* data, size_t size);

    void set_mesh(Mesh* mesh) { mesh_ = mesh; }
    Mesh* get_mesh() const { return mesh_; }

    void set_material(Material* material);
    Material* get_material() const noexcept { return material_; }

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

    void upload_material_parameters(bool force_upload = false);

private:
    void sync_render_component_material_buffer(RenderComponent& render_component);

    GeometryDataSetup geometry_data_setup_;
    UpdatePushConstant update_push_constant_function_ = nullptr;
    std::byte* push_constant_data_ = nullptr;

    Material* material_ = nullptr;
    Mesh* mesh_ = nullptr;
    uint32_t entity_index_ = InvalidUI32;
    bool render_component_initialized_ = false;
    uint32_t last_push_constant_transform_version_ = InvalidUI32;
};

}// namespace ocarina
