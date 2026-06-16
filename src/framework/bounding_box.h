#pragma once

#include "math/basic_types.h"
#include <cmath>
#include <limits>

namespace ocarina {

struct Frustum;

struct BoundingBox {
    float3 min{0.0f, 0.0f, 0.0f};
    float3 max{0.0f, 0.0f, 0.0f};
    bool valid = false;

    void reset() noexcept {
        min = make_float3(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        max = make_float3(
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max());
        valid = false;
    }

    void expand(const float3& point) noexcept {
        if (!valid) {
            min = point;
            max = point;
            valid = true;
            return;
        }
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    void merge(const BoundingBox& other) noexcept {
        if (!other.valid) {
            return;
        }
        if (!valid) {
            *this = other;
            return;
        }
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);
        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    [[nodiscard]] float3 center() const noexcept {
        return make_float3(
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f);
    }

    [[nodiscard]] int longest_axis() const noexcept {
        const float extent_x = max.x - min.x;
        const float extent_y = max.y - min.y;
        const float extent_z = max.z - min.z;
        if (extent_x >= extent_y && extent_x >= extent_z) {
            return 0;
        }
        if (extent_y >= extent_z) {
            return 1;
        }
        return 2;
    }

    [[nodiscard]] BoundingBox transformed(const float4x4& matrix) const noexcept;

    [[nodiscard]] bool intersects(const Frustum& frustum) const noexcept;
};

}// namespace ocarina
