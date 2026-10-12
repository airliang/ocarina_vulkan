#pragma once

#include "core/stl.h"
#include "core/header.h"
#include "core/hash.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class Device;
class Material;
class Mesh;

/// Per-entity render state (mesh/material pointers + push-constant scratch).
/// Mesh and Material instances are shared across entities; RenderComponent is not.
struct RenderComponent {
    /// Optional callback to stage push-constant members via set_push_constant_variable
    /// (shader reflection), same pattern as Material::set_property. No TransformComponent —
    /// per-entity data (e.g. transform_index) is supplied each frame via entity_index.
    using UpdatePushConstantFn =
        ocarina::function<void(RenderComponent& render, uint32_t entity_index)>;

    uint32_t mesh_id = InvalidUI32;
    Material* material = nullptr;

    /// Push-constant blocks from material shader reflection (not pipeline layout).
    ocarina_vector<PushConstantRange> push_constants;

    bool initialized = false;
    UpdatePushConstantFn update_push_constant_function;

    void set_mesh(Mesh* mesh);
    void set_material(Material* material);

    [[nodiscard]] Mesh* get_mesh() const noexcept;
    [[nodiscard]] Material* get_material() const noexcept { return material; }

    void set_update_push_constant_function(UpdatePushConstantFn func) {
        update_push_constant_function = std::move(func);
        initialized = false;
    }

    void ensure_push_constants_from_shaders();
    void set_push_constant_variable(uint64_t name_id, const std::byte* data, size_t size);
    void set_push_constant_variable(const char* name, const std::byte* data, size_t size);

    template<typename T>
    void set_push_constant_variable(uint64_t name_id, const T& value) {
        set_push_constant_variable(
            name_id,
            reinterpret_cast<const std::byte*>(&value),
            sizeof(T));
    }

    template<typename T>
    void set_push_constant_variable(const char* name, const T& value) {
        set_push_constant_variable(hash64(name), value);
    }

    void initialize(Device* device);
    void update_push_constants(uint32_t entity_index);
    void update(Device* device, uint32_t entity_index);
};

}// namespace ocarina
