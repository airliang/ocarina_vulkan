#include "scene.h"
#include "camera.h"
#include "entity_component_system.h"
#include "mesh.h"
#include "simd_frustum_cull.h"
#include <algorithm>
#include <numeric>

namespace ocarina {

namespace {

[[nodiscard]] BoundingBox get_primitive_world_bounds(Primitive& primitive, const TransformComponent& transform) {
    Mesh* mesh = primitive.get_mesh();
    if (mesh == nullptr || !mesh->has_local_bounds()) {
        return {};
    }
    return mesh->get_local_bounds().transformed(transform.get_world_matrix());
}

[[nodiscard]] math3d::Matrix4 multiply_matrix(const math3d::Matrix4& lhs, const math3d::Matrix4& rhs) {
    return lhs * rhs;
}

[[nodiscard]] BoundingBox compute_bounds_for_range(
    const std::vector<uint32_t>& entity_indices,
    uint32_t begin,
    uint32_t end,
    const std::vector<BoundingBox>& entity_bounds) {
    BoundingBox bounds;
    for (uint32_t index = begin; index < end; ++index) {
        bounds.merge(entity_bounds[entity_indices[index]]);
    }
    return bounds;
}

}// namespace

void Scene::clear_entities() {
    entity_indices_.clear();
    grid_cells_.clear();
    visible_cell_indices_.clear();
    visible_cell_count_ = 0;
    grid_dim_x_ = 0;
    grid_dim_z_ = 0;
    grid_origin_cell_x_ = 0;
    grid_origin_cell_z_ = 0;
    grid_cell_size_ = kMinGridCellSizeMeters;
    grid_built_ = false;
}

BoundingBox Scene::compute_entity_bounds(uint32_t entity_index) const {
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    if (entity_index >= ecs.primitive_count()) {
        return {};
    }
    return get_primitive_world_bounds(
        ecs.primitive(entity_index),
        ecs.transform_component(entity_index));
}

BoundingBox Scene::compute_bounds(const std::vector<uint32_t>& entity_indices) const {
    BoundingBox bounds;
    for (uint32_t entity_index : entity_indices) {
        bounds.merge(compute_entity_bounds(entity_index));
    }
    return bounds;
}

void Scene::ensure_visible_cell_capacity() {
    if (visible_cell_indices_.size() < grid_cells_.size()) {
        visible_cell_indices_.resize(grid_cells_.size());
    }
}

void Scene::build_grid(float cell_size_meters) {
    if (entity_indices_.empty()) {
        grid_cells_.clear();
        grid_built_ = false;
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    grid_cell_size_ = std::max(cell_size_meters, kMinGridCellSizeMeters);

    int32_t min_cell_x = INT32_MAX;
    int32_t min_cell_z = INT32_MAX;
    int32_t max_cell_x = INT32_MIN;
    int32_t max_cell_z = INT32_MIN;

    auto to_cell = [&](const float3& position) -> std::pair<int32_t, int32_t> {
        const int32_t cx = static_cast<int32_t>(std::floor(position.x / grid_cell_size_));
        const int32_t cz = static_cast<int32_t>(std::floor(position.z / grid_cell_size_));
        return {cx, cz};
    };

    // First pass: compute grid extents (XZ) from entity centers.
    for (uint32_t scene_index = 0; scene_index < entity_indices_.size(); ++scene_index) {
        const uint32_t entity_index = entity_indices_[scene_index];
        const BoundingBox bounds = compute_entity_bounds(entity_index);
        const float3 center = bounds.valid
            ? bounds.center()
            : ecs.transform_component(entity_index).get_position();
        const auto [cx, cz] = to_cell(center);
        min_cell_x = std::min(min_cell_x, cx);
        min_cell_z = std::min(min_cell_z, cz);
        max_cell_x = std::max(max_cell_x, cx);
        max_cell_z = std::max(max_cell_z, cz);
    }

    grid_origin_cell_x_ = min_cell_x;
    grid_origin_cell_z_ = min_cell_z;
    grid_dim_x_ = static_cast<uint32_t>(std::max(1, max_cell_x - min_cell_x + 1));
    grid_dim_z_ = static_cast<uint32_t>(std::max(1, max_cell_z - min_cell_z + 1));

    grid_cells_.clear();
    grid_cells_.resize(static_cast<size_t>(grid_dim_x_) * static_cast<size_t>(grid_dim_z_));
    for (SceneGridCell& cell : grid_cells_) {
        cell.bounds = {};
        cell.first_entity_index = 0;
        cell.entity_count = 0;
    }
    ensure_visible_cell_capacity();
    visible_cell_count_ = 0;

    auto cell_index = [&](int32_t cx, int32_t cz) -> uint32_t {
        const uint32_t ix = static_cast<uint32_t>(cx - grid_origin_cell_x_);
        const uint32_t iz = static_cast<uint32_t>(cz - grid_origin_cell_z_);
        return iz * grid_dim_x_ + ix;
    };

    // Second pass: count entities per cell and build per-cell bounds.
    std::vector<uint32_t> cell_counts(grid_cells_.size(), 0u);
    std::vector<BoundingBox> cell_bounds(grid_cells_.size());
    std::vector<uint32_t> entity_cell_ids(entity_indices_.size());
    for (uint32_t scene_index = 0; scene_index < entity_indices_.size(); ++scene_index) {
        const uint32_t entity_index = entity_indices_[scene_index];
        const BoundingBox bounds = compute_entity_bounds(entity_index);
        const float3 center = bounds.valid
            ? bounds.center()
            : ecs.transform_component(entity_index).get_position();
        const auto [cx, cz] = to_cell(center);
        const uint32_t flat = cell_index(cx, cz);
        entity_cell_ids[scene_index] = flat;
        cell_counts[flat] += 1;
        cell_bounds[flat].merge(bounds);
    }

    // Compute per-cell ranges (prefix sums) into the reordered entity_indices_ array.
    uint32_t running = 0;
    for (uint32_t flat = 0; flat < static_cast<uint32_t>(grid_cells_.size()); ++flat) {
        SceneGridCell& cell = grid_cells_[flat];
        cell.first_entity_index = running;
        cell.entity_count = cell_counts[flat];
        cell.bounds = cell_bounds[flat];
        running += cell.entity_count;
    }

    // Third pass: reorder entity_indices_ so entities are grouped by cell.
    std::vector<uint32_t> write_cursor(grid_cells_.size(), 0u);
    for (uint32_t flat = 0; flat < static_cast<uint32_t>(grid_cells_.size()); ++flat) {
        write_cursor[flat] = grid_cells_[flat].first_entity_index;
    }
    std::vector<uint32_t> reordered(entity_indices_.size());
    for (uint32_t scene_index = 0; scene_index < static_cast<uint32_t>(entity_indices_.size()); ++scene_index) {
        const uint32_t flat = entity_cell_ids[scene_index];
        const uint32_t dst = write_cursor[flat]++;
        reordered[dst] = entity_indices_[scene_index];
    }
    entity_indices_.swap(reordered);

    grid_built_ = true;
}

void Scene::build_primitive_cull_batch() {
    // No-op: entity ordering is stored in grid cell ranges.
}

void Scene::cull_grids(const Frustum& frustum) {
    visible_cell_count_ = 0;

    if (!grid_built_ || grid_cells_.empty()) {
        return;
    }

    ensure_visible_cell_capacity();

    // Pass 1: CullCellsSIMD. Pack visible cell flat indices into visible_cell_indices_ using visible_cell_count_.
    const uint32_t cell_count = static_cast<uint32_t>(grid_cells_.size());
    uint32_t cell_index_i = 0;

    alignas(16) float min_x[4]{};
    alignas(16) float min_y[4]{};
    alignas(16) float min_z[4]{};
    alignas(16) float max_x[4]{};
    alignas(16) float max_y[4]{};
    alignas(16) float max_z[4]{};
    alignas(16) float valid_mask_f[4]{};
    uint32_t flat_lane_index[4]{};

    for (; cell_index_i + 4 <= cell_count; cell_index_i += 4) {
        for (int lane = 0; lane < 4; ++lane) {
            const uint32_t flat = cell_index_i + static_cast<uint32_t>(lane);
            flat_lane_index[lane] = flat;
            const SceneGridCell& cell = grid_cells_[flat];
            valid_mask_f[lane] = 0.0f;

            if (cell.entity_count == 0) {
                min_x[lane] = min_y[lane] = min_z[lane] = 0.0f;
                max_x[lane] = max_y[lane] = max_z[lane] = 0.0f;
                continue;
            }

            if (!cell.bounds.valid) {
                // No bounds: conservatively treat as visible.
                valid_mask_f[lane] = -1.0f;
                min_x[lane] = min_y[lane] = min_z[lane] = 0.0f;
                max_x[lane] = max_y[lane] = max_z[lane] = 0.0f;
                continue;
            }

            min_x[lane] = cell.bounds.min.x;
            min_y[lane] = cell.bounds.min.y;
            min_z[lane] = cell.bounds.min.z;
            max_x[lane] = cell.bounds.max.x;
            max_y[lane] = cell.bounds.max.y;
            max_z[lane] = cell.bounds.max.z;
        }

        const uint32_t visible_mask = intersect_aabb4_soa(
            frustum,
            _mm_load_ps(min_x),
            _mm_load_ps(min_y),
            _mm_load_ps(min_z),
            _mm_load_ps(max_x),
            _mm_load_ps(max_y),
            _mm_load_ps(max_z),
            _mm_load_ps(valid_mask_f));

        for (int lane = 0; lane < 4; ++lane) {
            const uint32_t flat = flat_lane_index[lane];
            if (grid_cells_[flat].entity_count == 0) {
                continue;
            }
            if ((visible_mask & (1u << lane)) == 0u) {
                continue;
            }
            visible_cell_indices_[visible_cell_count_++] = flat;
        }
    }

    for (; cell_index_i < cell_count; ++cell_index_i) {
        const SceneGridCell& cell = grid_cells_[cell_index_i];
        if (cell.entity_count == 0) {
            continue;
        }
        if (!cell.bounds.valid || cell.bounds.intersects(frustum)) {
            visible_cell_indices_[visible_cell_count_++] = cell_index_i;
        }
    }
}

void Scene::cull_grids(Camera& camera) {
    const math3d::Matrix4 view_projection = multiply_matrix(
        camera.get_projection_matrix(),
        camera.get_view_matrix());
    cull_grids(Frustum(view_projection));
}

}// namespace ocarina
