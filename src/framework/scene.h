#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "math.h"
#include "entity_component_system.h"
#include "primitive.h"
#include "transform_component.h"
#include <utility>

namespace ocarina {

class Camera;

struct SceneGridCell {
    BoundingBox bounds;
    uint32_t first_entity_index = 0; // range into Scene::entity_indices_
    uint32_t entity_count = 0;
};

class OC_FRAMEWORK_API Scene {
public:
    static constexpr size_t kMaxPrimitivesPerCullBatch = 100;
    // Frostbite-like 2D grid culling: cell size is at least 20m x 20m (XZ plane).
    static constexpr float kMinGridCellSizeMeters = 20.0f;
    static constexpr float kDefaultGridCellSizeMeters = 64.0f;

    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    template<typename... Args>
    Primitive& emplace_primitive(Args&&... args) {
        grid_built_ = false;
        const uint32_t entity_index = EntityComponentSystem::instance().emplace_primitive(OC_FORWARD(args)...);
        entity_indices_.push_back(entity_index);
        return EntityComponentSystem::instance().primitive(entity_index);
    }

    void clear_entities();

    // Builds a 2D grid spatial index on XZ plane.
    void build_grid(float cell_size_meters = kDefaultGridCellSizeMeters);

    // Pass 1: cull cells (SIMD) and output a visible cell index list.
    void cull_grids(Camera& camera);
    void cull_grids(const Frustum& frustum);

    void build_primitive_cull_batch();

    void reserve_entities(size_t count) { entity_indices_.reserve(count); }
    // Backward-compatible alias (some tests call this).
    void reserve_primitives(size_t count) { reserve_entities(count); }

    [[nodiscard]] uint32_t entity_index(uint32_t scene_index) const {
        return entity_indices_[scene_index];
    }

    [[nodiscard]] Primitive& primitive(uint32_t scene_index) {
        return EntityComponentSystem::instance().primitive(entity_indices_[scene_index]);
    }

    [[nodiscard]] const Primitive& primitive(uint32_t scene_index) const {
        return EntityComponentSystem::instance().primitive(entity_indices_[scene_index]);
    }

    [[nodiscard]] uint32_t primitive_count() const noexcept {
        return static_cast<uint32_t>(entity_indices_.size());
    }

    [[nodiscard]] TransformComponent& transform_component(uint32_t scene_index) {
        return EntityComponentSystem::instance().transform_component(entity_indices_[scene_index]);
    }

    [[nodiscard]] const TransformComponent& transform_component(uint32_t scene_index) const {
        return EntityComponentSystem::instance().transform_component(entity_indices_[scene_index]);
    }

    [[nodiscard]] const std::vector<uint32_t>& entity_indices() const noexcept {
        return entity_indices_;
    }

    [[nodiscard]] bool has_grid() const noexcept { return grid_built_; }
    [[nodiscard]] uint32_t grid_cell_count() const noexcept { return static_cast<uint32_t>(grid_cells_.size()); }
    [[nodiscard]] uint32_t visible_cell_count() const noexcept { return visible_cell_count_; }
    [[nodiscard]] const std::vector<uint32_t>& visible_cell_indices() const noexcept { return visible_cell_indices_; }
    // Legacy aliases used by the culling test UI.
    [[nodiscard]] uint32_t visible_grid_count() const noexcept { return visible_cell_count_; }
    [[nodiscard]] const std::vector<uint32_t>& visible_grid_indices() const noexcept { return visible_cell_indices_; }

    // Convert a flat cell index to world cell coordinates (cx, cz).
    [[nodiscard]] std::pair<int32_t, int32_t> grid_cell_coords(uint32_t flat_index) const noexcept {
        if (grid_dim_x_ == 0) {
            return {grid_origin_cell_x_, grid_origin_cell_z_};
        }
        const uint32_t iz = flat_index / grid_dim_x_;
        const uint32_t ix = flat_index - iz * grid_dim_x_;
        return {grid_origin_cell_x_ + static_cast<int32_t>(ix), grid_origin_cell_z_ + static_cast<int32_t>(iz)};
    }

    [[nodiscard]] uint32_t grid_cell_entity_count(uint32_t flat_index) const noexcept {
        return flat_index < grid_cells_.size() ? grid_cells_[flat_index].entity_count : 0u;
    }

    [[nodiscard]] const std::vector<SceneGridCell>& grid_cells() const noexcept {
        return grid_cells_;
    }

private:
    [[nodiscard]] BoundingBox compute_entity_bounds(uint32_t entity_index) const;
    [[nodiscard]] BoundingBox compute_bounds(const std::vector<uint32_t>& entity_indices) const;
    void ensure_visible_cell_capacity();

    std::vector<uint32_t> entity_indices_;

    // Grid data (XZ plane).
    std::vector<SceneGridCell> grid_cells_;
    std::vector<uint32_t> visible_cell_indices_;
    uint32_t visible_cell_count_ = 0;
    uint32_t grid_dim_x_ = 0;
    uint32_t grid_dim_z_ = 0;
    int32_t grid_origin_cell_x_ = 0;
    int32_t grid_origin_cell_z_ = 0;
    float grid_cell_size_ = kMinGridCellSizeMeters;
    bool grid_built_ = false;
};

}// namespace ocarina
