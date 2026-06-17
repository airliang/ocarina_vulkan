#include "scene.h"
#include "camera.h"
#include "mesh.h"
#include <algorithm>

namespace ocarina {

namespace {

[[nodiscard]] BoundingBox get_primitive_world_bounds(Primitive& primitive) {
    Mesh* mesh = primitive.get_mesh();
    if (mesh == nullptr || !mesh->has_local_bounds()) {
        return {};
    }
    return mesh->get_local_bounds().transformed(primitive.get_world_matrix());
}

[[nodiscard]] math3d::Matrix4 multiply_matrix(const math3d::Matrix4& lhs, const math3d::Matrix4& rhs) {
    return lhs * rhs;
}

[[nodiscard]] BoundingBox compute_bounds_for_range(
    const std::vector<uint32_t>& primitive_indices,
    uint32_t begin,
    uint32_t end,
    const std::vector<BoundingBox>& primitive_bounds) {
    BoundingBox bounds;
    for (uint32_t index = begin; index < end; ++index) {
        bounds.merge(primitive_bounds[primitive_indices[index]]);
    }
    return bounds;
}

}// namespace

void Scene::clear_primitives() {
    primitives_.clear();
    visible_primitive_indices_.clear();
    primitive_cull_batch_.clear();
    need_cull_primitive_count_ = 0;
    grid_cells_.clear();
    visible_grid_indices_.clear();
    visible_grid_count_ = 0;
    grid_dim_x_ = 0;
    grid_dim_z_ = 0;
    grid_origin_cell_x_ = 0;
    grid_origin_cell_z_ = 0;
    grid_cell_size_ = kMinGridCellSizeMeters;
    grid_built_ = false;
}

BoundingBox Scene::compute_primitive_bounds(uint32_t primitive_index) const {
    if (primitive_index >= primitives_.size()) {
        return {};
    }
    return get_primitive_world_bounds(const_cast<Primitive&>(primitives_[primitive_index]));
}

BoundingBox Scene::compute_bounds(const std::vector<uint32_t>& primitive_indices) const {
    BoundingBox bounds;
    for (uint32_t primitive_index : primitive_indices) {
        bounds.merge(compute_primitive_bounds(primitive_index));
    }
    return bounds;
}

void Scene::ensure_primitive_cull_batch_capacity() {
    if (primitive_cull_batch_.size() != primitives_.size()) {
        primitive_cull_batch_.resize(primitives_.size());
    }
}

void Scene::build_grid(float cell_size_meters) {
    if (primitives_.empty()) {
        grid_cells_.clear();
        grid_built_ = false;
        return;
    }

    grid_cell_size_ = std::max(cell_size_meters, kMinGridCellSizeMeters);

    std::vector<BoundingBox> primitive_bounds(primitives_.size());
    int32_t min_cell_x = INT32_MAX;
    int32_t min_cell_z = INT32_MAX;
    int32_t max_cell_x = INT32_MIN;
    int32_t max_cell_z = INT32_MIN;

    auto to_cell = [&](const float3& position) -> std::pair<int32_t, int32_t> {
        const int32_t cx = static_cast<int32_t>(std::floor(position.x / grid_cell_size_));
        const int32_t cz = static_cast<int32_t>(std::floor(position.z / grid_cell_size_));
        return {cx, cz};
    };

    for (uint32_t index = 0; index < primitives_.size(); ++index) {
        primitive_bounds[index] = compute_primitive_bounds(index);
        const BoundingBox& b = primitive_bounds[index];
        const float3 center = b.valid ? b.center() : primitives_[index].get_position();
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
        cell.primitive_indices.clear();
    }
    visible_grid_indices_.clear();
    visible_grid_indices_.reserve(grid_cells_.size());
    visible_grid_count_ = 0;

    auto cell_index = [&](int32_t cx, int32_t cz) -> uint32_t {
        const uint32_t ix = static_cast<uint32_t>(cx - grid_origin_cell_x_);
        const uint32_t iz = static_cast<uint32_t>(cz - grid_origin_cell_z_);
        return iz * grid_dim_x_ + ix;
    };

    // Reserve roughly: average primitives per cell might be unknown, but reserving a bit helps.
    for (uint32_t index = 0; index < primitives_.size(); ++index) {
        const BoundingBox& b = primitive_bounds[index];
        const float3 center = b.valid ? b.center() : primitives_[index].get_position();
        const auto [cx, cz] = to_cell(center);
        SceneGridCell& cell = grid_cells_[cell_index(cx, cz)];
        cell.primitive_indices.push_back(index);
        cell.bounds.merge(b);
    }

    ensure_primitive_cull_batch_capacity();
    grid_built_ = true;
}

void Scene::build_primitive_cull_batch() {
    // With grid culling, primitive_cull_batch_ is already packed in cull_grids().
}

void Scene::set_visible_primitive_indices(const std::vector<uint32_t>& indices, uint32_t count) {
    visible_primitive_indices_.assign(indices.begin(), indices.begin() + count);
}

void Scene::make_all_primitives_visible() {
    set_all_primitives_visible();
    need_cull_primitive_count_ = 0;
}

void Scene::set_all_primitives_visible() {
    visible_primitive_indices_.resize(primitives_.size());
    for (uint32_t index = 0; index < primitives_.size(); ++index) {
        visible_primitive_indices_[index] = index;
    }
}

void Scene::cull_grids(const Frustum& frustum) {
    visible_primitive_indices_.clear();
    need_cull_primitive_count_ = 0;
    visible_grid_indices_.clear();
    visible_grid_count_ = 0;

    if (!grid_built_ || grid_cells_.empty()) {
        set_all_primitives_visible();
        return;
    }

    ensure_primitive_cull_batch_capacity();

    // Single loop over grid cells: cull cell AABB then pack indices.
    for (uint32_t cell_flat_index = 0; cell_flat_index < grid_cells_.size(); ++cell_flat_index) {
        const SceneGridCell& cell = grid_cells_[cell_flat_index];
        if (cell.primitive_indices.empty()) {
            continue;
        }
        if (!cell.bounds.valid || cell.bounds.intersects(frustum)) {
            visible_grid_indices_.push_back(cell_flat_index);
            ++visible_grid_count_;
            for (uint32_t primitive_index : cell.primitive_indices) {
                primitive_cull_batch_[need_cull_primitive_count_++] = primitive_index;
            }
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
