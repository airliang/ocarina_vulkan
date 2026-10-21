#pragma once

#include "core/stl.h"
#include "core/header.h"
#include "core/hash.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class Device;
class Material;
class Mesh;
class ShaderProgram;

/// Per-entity render state (mesh/material pointers + push-constant scratch).
/// Mesh and Material instances are shared across entities; RenderComponent is not.
struct RenderComponent {
    /// Optional callback to stage push-constant members via set_push_constant_variable
    /// (shader reflection). No TransformComponent — per-entity data (e.g. transform_index)
    /// is supplied via entity_index when the draw path refreshes a dirty program block.
    using UpdatePushConstantFn =
        ocarina::function<void(RenderComponent& render, uint32_t entity_index)>;

    struct PushConstantBlock {
        ocarina_vector<PushConstantRange> ranges;
        bool dirty = true;
    };

    uint32_t mesh_id = InvalidUI32;
    Material* material = nullptr;

    /// Per ShaderProgram* staging (a material may use different programs across passes).
    ocarina_unordered_map<ShaderProgram*, PushConstantBlock> push_constants_by_program;

    UpdatePushConstantFn update_push_constant_function;

    void set_mesh(Mesh* mesh);
    void set_material(Material* material);

    [[nodiscard]] Mesh* get_mesh() const noexcept;
    [[nodiscard]] Material* get_material() const noexcept { return material; }

    void set_update_push_constant_function(UpdatePushConstantFn func) {
        update_push_constant_function = std::move(func);
        mark_push_constants_dirty();
    }

    void mark_push_constants_dirty() noexcept;

    /// Ensure staging ranges exist for @p program (from program reflection).
    PushConstantBlock& ensure_push_constants(ShaderProgram* program);

    /// If the block for @p program is dirty, run the update callback then clear dirty.
    void update_push_constants_if_dirty(ShaderProgram* program, uint32_t entity_index);

    [[nodiscard]] const ocarina_vector<PushConstantRange>* push_constants_for(
        ShaderProgram* program) const noexcept;

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

private:
    /// Program whose ranges set_push_constant_variable writes into during an update.
    ShaderProgram* active_push_constant_program_ = nullptr;
};

}// namespace ocarina
