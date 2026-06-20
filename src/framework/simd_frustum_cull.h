#pragma once

#include "frustum.h"
#include "bounding_box.h"
#include <cstdint>
#include <cmath>
#include <immintrin.h>

namespace ocarina {

// Frostbite-style SoA SIMD frustum culling helpers.
// Reference: "Culling the Battlefield: Data Oriented Design in Practice" (Daniel Collin, DICE, GDC 2011)
//
// When sphere/AABB data is already in SoA layout, broadcast each frustum plane and test 4 objects
// per instruction stream with minimal branching.

inline __m128 load4_or_zero(const float* src, bool has_data) noexcept {
    if (has_data) {
        return _mm_loadu_ps(src);
    }
    return _mm_setzero_ps();
}

inline __m128 load4_or_ps(const float* src, bool has_data, float fallback) noexcept {
    if (has_data) {
        return _mm_loadu_ps(src);
    }
    return _mm_set1_ps(fallback);
}

// Bounding sphere from AABB (conservative). Matches Frostbite grid sphere tests.
inline void bounding_sphere_from_aabb(const BoundingBox& bounds, float& cx, float& cy, float& cz, float& radius) noexcept {
    const float3 center = bounds.center();
    cx = center.x;
    cy = center.y;
    cz = center.z;
    const float hx = bounds.max.x - center.x;
    const float hy = bounds.max.y - center.y;
    const float hz = bounds.max.z - center.z;
    radius = std::sqrt(hx * hx + hy * hy + hz * hz);
}

// 4-wide sphere vs frustum. cx/cy/cz/cr hold 4 spheres; valid_mask lane = 0 => always visible.
inline uint32_t intersect_sphere4_soa(
    const Frustum& frustum,
    __m128 cx,
    __m128 cy,
    __m128 cz,
    __m128 cr,
    __m128 valid_mask) noexcept {
    __m128 visible = _mm_castsi128_ps(_mm_set1_epi32(-1));

    for (int plane_index = 0; plane_index < Frustum::plane_count; ++plane_index) {
        const FrustumPlane& plane = frustum.plane(plane_index);
        __m128 dot = _mm_mul_ps(cx, _mm_set1_ps(plane.normal.x));
        dot = _mm_add_ps(dot, _mm_mul_ps(cy, _mm_set1_ps(plane.normal.y)));
        dot = _mm_add_ps(dot, _mm_mul_ps(cz, _mm_set1_ps(plane.normal.z)));
        dot = _mm_add_ps(dot, _mm_set1_ps(plane.distance));

        const __m128 outside = _mm_cmpgt_ps(dot, cr);
        visible = _mm_andnot_ps(outside, visible);
    }

    int mask = _mm_movemask_ps(visible);
    mask |= _mm_movemask_ps(valid_mask);
    return static_cast<uint32_t>(mask);
}

// 4-wide AABB vs frustum (positive-vertex test). More accurate than sphere for boxes.
inline uint32_t intersect_aabb4_soa(
    const Frustum& frustum,
    __m128 min_x,
    __m128 min_y,
    __m128 min_z,
    __m128 max_x,
    __m128 max_y,
    __m128 max_z,
    __m128 valid_mask) noexcept {
    __m128 visible = _mm_castsi128_ps(_mm_set1_epi32(-1));

    for (int plane_index = 0; plane_index < Frustum::plane_count; ++plane_index) {
        const FrustumPlane& plane = frustum.plane(plane_index);
        const __m128 px = (plane.normal.x >= 0.0f) ? max_x : min_x;
        const __m128 py = (plane.normal.y >= 0.0f) ? max_y : min_y;
        const __m128 pz = (plane.normal.z >= 0.0f) ? max_z : min_z;

        __m128 dot = _mm_mul_ps(px, _mm_set1_ps(plane.normal.x));
        dot = _mm_add_ps(dot, _mm_mul_ps(py, _mm_set1_ps(plane.normal.y)));
        dot = _mm_add_ps(dot, _mm_mul_ps(pz, _mm_set1_ps(plane.normal.z)));
        dot = _mm_add_ps(dot, _mm_set1_ps(plane.distance));

        const __m128 outside = _mm_cmplt_ps(dot, _mm_setzero_ps());
        visible = _mm_andnot_ps(outside, visible);
    }

    int mask = _mm_movemask_ps(visible);
    mask |= _mm_movemask_ps(valid_mask);
    return static_cast<uint32_t>(mask);
}

}// namespace ocarina
