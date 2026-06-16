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
    root_cluster_ = {};
    visible_leaf_clusters_.clear();
    visible_primitive_indices_.clear();
    primitive_cull_batch_.clear();
    need_cull_primitive_count_ = 0;
    cluster_built_ = false;
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

SceneCluster Scene::build_cluster_recursive(
    std::vector<uint32_t>& primitive_indices,
    uint32_t begin,
    uint32_t end,
    uint32_t depth,
    const std::vector<BoundingBox>& primitive_bounds) const {
    SceneCluster cluster;
    const uint32_t count = end - begin;
    cluster.bounds = compute_bounds_for_range(primitive_indices, begin, end, primitive_bounds);

    if (count <= kMaxLeafPrimitives || depth >= kMaxClusterDepth) {
        cluster.primitive_indices.assign(primitive_indices.begin() + begin, primitive_indices.begin() + end);
        return cluster;
    }

    const int axis = cluster.bounds.longest_axis();

    auto center_on_axis = [&](uint32_t primitive_index) -> float {
        const BoundingBox& bounds = primitive_bounds[primitive_index];
        return bounds.valid ? bounds.center()[axis] : cluster.bounds.center()[axis];
    };

    const uint32_t mid_index = begin + count / 2;
    std::nth_element(
        primitive_indices.begin() + begin,
        primitive_indices.begin() + mid_index,
        primitive_indices.begin() + end,
        [&](uint32_t lhs, uint32_t rhs) {
            return center_on_axis(lhs) < center_on_axis(rhs);
        });

    const uint32_t split = begin + count / 2;
    if (split <= begin || split >= end) {
        cluster.primitive_indices.assign(primitive_indices.begin() + begin, primitive_indices.begin() + end);
        return cluster;
    }

    cluster.children.reserve(2);
    cluster.children.push_back(build_cluster_recursive(primitive_indices, begin, split, depth + 1, primitive_bounds));
    cluster.children.push_back(build_cluster_recursive(primitive_indices, split, end, depth + 1, primitive_bounds));
    return cluster;
}

void Scene::build_cluster_hierarchy() {
    if (primitives_.empty()) {
        root_cluster_ = {};
        cluster_built_ = false;
        return;
    }

    std::vector<uint32_t> primitive_indices(primitives_.size());
    std::vector<BoundingBox> primitive_bounds(primitives_.size());
    for (uint32_t index = 0; index < primitives_.size(); ++index) {
        primitive_indices[index] = index;
        primitive_bounds[index] = compute_primitive_bounds(index);
    }

    primitive_cull_batch_.resize(primitives_.size());
    root_cluster_ = build_cluster_recursive(primitive_indices, 0, static_cast<uint32_t>(primitive_indices.size()), 0, primitive_bounds);
    cluster_built_ = true;
}

void Scene::cull_clusters_recursive(const SceneCluster& cluster, const Frustum& frustum) {
    if (!cluster.bounds.intersects(frustum)) {
        return;
    }

    if (cluster.is_leaf()) {
        visible_leaf_clusters_.push_back(&cluster);
        return;
    }

    for (const SceneCluster& child : cluster.children) {
        cull_clusters_recursive(child, frustum);
    }
}

void Scene::build_primitive_cull_batch() {
    need_cull_primitive_count_ = 0;
    for (const SceneCluster* leaf_cluster : visible_leaf_clusters_) {
        for (uint32_t primitive_index : leaf_cluster->primitive_indices) {
            primitive_cull_batch_[need_cull_primitive_count_++] = primitive_index;
        }
    }
}

void Scene::set_visible_primitive_indices(const std::vector<uint32_t>& indices, uint32_t count) {
    visible_primitive_indices_.assign(indices.begin(), indices.begin() + count);
}

void Scene::make_all_primitives_visible() {
    set_all_primitives_visible();
    visible_leaf_clusters_.clear();
    need_cull_primitive_count_ = 0;
}

void Scene::set_all_primitives_visible() {
    visible_primitive_indices_.resize(primitives_.size());
    for (uint32_t index = 0; index < primitives_.size(); ++index) {
        visible_primitive_indices_[index] = index;
    }
}

void Scene::cull_clusters(const Frustum& frustum) {
    visible_primitive_indices_.clear();
    visible_leaf_clusters_.clear();

    if (!cluster_built_ || !root_cluster_.bounds.valid) {
        set_all_primitives_visible();
        return;
    }

    if (!root_cluster_.bounds.intersects(frustum)) {
        return;
    }

    cull_clusters_recursive(root_cluster_, frustum);
}

void Scene::cull_clusters(Camera& camera) {
    const math3d::Matrix4 view_projection = multiply_matrix(
        camera.get_projection_matrix(),
        camera.get_view_matrix());
    cull_clusters(Frustum(view_projection));
}

}// namespace ocarina
