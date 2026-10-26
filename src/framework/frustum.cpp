#include "frustum.h"
#include <cmath>

namespace ocarina {

void Frustum::normalize_plane(int index) {
    const float length = std::sqrt(
        planes_[index].normal[0] * planes_[index].normal[0] +
        planes_[index].normal[1] * planes_[index].normal[1] +
        planes_[index].normal[2] * planes_[index].normal[2]);
    if (length > 0.0f) {
        const float inv_length = 1.0f / length;
        planes_[index].normal[0] *= inv_length;
        planes_[index].normal[1] *= inv_length;
        planes_[index].normal[2] *= inv_length;
        planes_[index].distance *= inv_length;
    }
}

void Frustum::extract_from_matrix(const ktm::fmat4x4& matrix) {
    const auto row = [&matrix](int r) {
        return ktm::fvec4{matrix[0][r], matrix[1][r], matrix[2][r], matrix[3][r]};
    };
    const ktm::fvec4 row0 = row(0);
    const ktm::fvec4 row1 = row(1);
    const ktm::fvec4 row2 = row(2);
    const ktm::fvec4 row3 = row(3);
    const ktm::fvec4 plane_rows[plane_count] = {
        row3 + row0,
        row3 - row0,
        row3 + row1,
        row3 - row1,
        row3 + row2,
        row3 - row2,
    };

    for (int i = 0; i < plane_count; ++i) {
        planes_[i].normal = make_float3(plane_rows[i][0], plane_rows[i][1], plane_rows[i][2]);
        planes_[i].distance = plane_rows[i][3];
        normalize_plane(i);
    }
}

bool Frustum::intersects(const BoundingBox& bounds) const noexcept {
    if (!bounds.valid) {
        return true;
    }

    const float corners[8][3] = {
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    };

    for (int plane_index = 0; plane_index < plane_count; ++plane_index) {
        const FrustumPlane& plane = planes_[plane_index];
        bool all_outside = true;
        for (int corner_index = 0; corner_index < 8; ++corner_index) {
            const float3 point = {
                corners[corner_index][0],
                corners[corner_index][1],
                corners[corner_index][2]};
            if (plane.signed_distance(point) >= 0.0f) {
                all_outside = false;
                break;
            }
        }
        if (all_outside) {
            return false;
        }
    }

    return true;
}

BoundingBox BoundingBox::transformed(const float4x4& matrix) const noexcept {
    BoundingBox result;
    if (!valid) {
        return result;
    }

    const float xs[2] = {min.x, max.x};
    const float ys[2] = {min.y, max.y};
    const float zs[2] = {min.z, max.z};

    for (float x : xs) {
        for (float y : ys) {
            for (float z : zs) {
                const float4 point = matrix * make_float4(x, y, z, 1.0f);
                result.expand(make_float3(point.x, point.y, point.z));
            }
        }
    }

    return result;
}

bool BoundingBox::intersects(const Frustum& frustum) const noexcept {
    return frustum.intersects(*this);
}

}// namespace ocarina
