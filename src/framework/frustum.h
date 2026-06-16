#pragma once

#include "math/basic_types.h"
#include "math.h"
#include "bounding_box.h"

namespace ocarina {

struct FrustumPlane {
    float3 normal{};
    float distance = 0.0f;

    [[nodiscard]] float signed_distance(const float3& point) const noexcept {
        return dot(normal, point) + distance;
    }
};

class Frustum {
public:
    static constexpr int plane_count = 6;

    Frustum() = default;

    explicit Frustum(const math3d::Matrix4& view_projection) {
        extract_from_matrix(view_projection);
    }

    [[nodiscard]] const FrustumPlane& plane(int index) const noexcept { return planes_[index]; }

    [[nodiscard]] bool intersects(const BoundingBox& bounds) const noexcept;

private:
    void extract_from_matrix(const math3d::Matrix4& matrix);
    void normalize_plane(int index);

    FrustumPlane planes_[plane_count]{};
};

}// namespace ocarina
