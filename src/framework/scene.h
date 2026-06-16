#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "math.h"
#include "primitive.h"

namespace ocarina {

class Camera;

struct SceneCluster {
    BoundingBox bounds;
    std::vector<SceneCluster> children;
    std::vector<uint32_t> primitive_indices;

    [[nodiscard]] bool is_leaf() const noexcept { return children.empty(); }
};

class OC_FRAMEWORK_API Scene {
public:
    static constexpr size_t kMaxLeafPrimitives = 64;
    static constexpr size_t kMaxPrimitivesPerCullBatch = 100;
    static constexpr uint32_t kMaxClusterDepth = 6;

    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    template<typename... Args>
    Primitive& emplace_primitive(Args&&... args) {
        cluster_built_ = false;
        return primitives_.emplace_back(OC_FORWARD(args)...);
    }

    void clear_primitives();

    void build_cluster_hierarchy();

    void cull_clusters(Camera& camera);
    void cull_clusters(const Frustum& frustum);

    void build_primitive_cull_batch();
    void set_visible_primitive_indices(const std::vector<uint32_t>& indices, uint32_t count);
    void make_all_primitives_visible();

    void reserve_primitives(size_t count) { primitives_.reserve(count); }

    [[nodiscard]] const std::vector<Primitive>& primitives() const noexcept { return primitives_; }
    [[nodiscard]] std::vector<Primitive>& primitives() noexcept { return primitives_; }
    [[nodiscard]] Primitive& primitive(uint32_t index) { return primitives_[index]; }
    [[nodiscard]] const Primitive& primitive(uint32_t index) const { return primitives_[index]; }
    [[nodiscard]] uint32_t primitive_count() const noexcept { return static_cast<uint32_t>(primitives_.size()); }

    [[nodiscard]] const std::vector<uint32_t>& visible_primitive_indices() const noexcept {
        return visible_primitive_indices_;
    }
    [[nodiscard]] const std::vector<uint32_t>& primitive_cull_batch() const noexcept { return primitive_cull_batch_; }
    [[nodiscard]] uint32_t need_cull_primitive_count() const noexcept { return need_cull_primitive_count_; }
    [[nodiscard]] const SceneCluster& root_cluster() const noexcept { return root_cluster_; }
    [[nodiscard]] bool has_cluster_hierarchy() const noexcept { return cluster_built_; }

private:
    [[nodiscard]] BoundingBox compute_primitive_bounds(uint32_t primitive_index) const;
    [[nodiscard]] BoundingBox compute_bounds(const std::vector<uint32_t>& primitive_indices) const;
    [[nodiscard]] SceneCluster build_cluster_recursive(
        std::vector<uint32_t>& primitive_indices,
        uint32_t begin,
        uint32_t end,
        uint32_t depth,
        const std::vector<BoundingBox>& primitive_bounds) const;

    void cull_clusters_recursive(const SceneCluster& cluster, const Frustum& frustum);
    void set_all_primitives_visible();

    std::vector<Primitive> primitives_;
    std::vector<const SceneCluster*> visible_leaf_clusters_;
    std::vector<uint32_t> visible_primitive_indices_;
    std::vector<uint32_t> primitive_cull_batch_;
    uint32_t need_cull_primitive_count_ = 0;

    SceneCluster root_cluster_;
    bool cluster_built_ = false;
};

}// namespace ocarina
