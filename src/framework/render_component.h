#pragma once

#include "core/stl.h"
#include "core/header.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class Device;
class Material;
class Mesh;
class TransformComponent;

/// Per-entity render state. Stored sparsely — only entities that add it allocate a slot.
struct RenderComponent {
    using UpdatePushConstantFn =
        ocarina::function<void(RenderComponent& render, TransformComponent& transform)>;

    uint32_t mesh_id = InvalidUI32;
    Material* material = nullptr;

    /// Push-constant blocks from material shader reflection (not pipeline layout).
    ocarina_vector<PushConstantRange> push_constants;

    bool initialized = false;
    uint32_t last_push_constant_transform_version = InvalidUI32;
    UpdatePushConstantFn update_push_constant_function;

    void set_mesh(Mesh* mesh);
    void set_material(Material* material);

    [[nodiscard]] Mesh* get_mesh() const noexcept;
    [[nodiscard]] Material* get_material() const noexcept { return material; }

    void set_update_push_constant_function(UpdatePushConstantFn func) {
        update_push_constant_function = std::move(func);
        last_push_constant_transform_version = InvalidUI32;
    }

    void ensure_push_constants_from_shaders();
    void set_push_constant_variable(uint64_t name_id, const std::byte* data, size_t size);
    void write_transform_index_push_constant(uint32_t entity_index);

    void initialize(Device* device, TransformComponent& transform, uint32_t entity_index);
    void update_push_constants(TransformComponent& transform, uint32_t entity_index);
    void update(Device* device, TransformComponent& transform, uint32_t entity_index);
};

}// namespace ocarina
