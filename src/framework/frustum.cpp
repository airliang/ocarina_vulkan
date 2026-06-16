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

void Frustum::extract_from_matrix(const math3d::Matrix4& matrix) {
    const float* m = matrix.m.data();

    planes_[0].normal = {m[12] + m[0], m[13] + m[1], m[14] + m[2]};
    planes_[0].distance = m[15] + m[3];

    planes_[1].normal = {m[12] - m[0], m[13] - m[1], m[14] - m[2]};
    planes_[1].distance = m[15] - m[3];

    planes_[2].normal = {m[12] + m[4], m[13] + m[5], m[14] + m[6]};
    planes_[2].distance = m[15] + m[7];

    planes_[3].normal = {m[12] - m[4], m[13] - m[5], m[14] - m[6]};
    planes_[3].distance = m[15] - m[7];

    planes_[4].normal = {m[12] + m[8], m[13] + m[9], m[14] + m[10]};
    planes_[4].distance = m[15] + m[11];

    planes_[5].normal = {m[12] - m[8], m[13] - m[9], m[14] - m[10]};
    planes_[5].distance = m[15] - m[11];

    for (int i = 0; i < plane_count; ++i) {
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
