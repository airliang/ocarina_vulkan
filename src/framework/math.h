//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include <ktm/ktm.h>

namespace ocarina {

// Left-handed, +X right, +Y up, +Z forward (DirectX-style).
// ktm::cross is the right-hand product. The previous camera basis used the
// opposite product, and ktm::look_at_lh would mirror the view on X. cross_lh
// keeps that basis so the rendered image stays the same.
[[nodiscard]] inline ktm::fvec3 cross_lh(const ktm::fvec3& a, const ktm::fvec3& b) noexcept {
    return -ktm::cross(a, b);
}

[[nodiscard]] inline ktm::fmat4x4 mat4_identity() noexcept {
    return ktm::fmat4x4::from_diag(ktm::fvec4{1.0f, 1.0f, 1.0f, 1.0f});
}

// Column-major. Translation is column 3. Matches the matrix previously uploaded
// to HLSL after transposing the row-major math3d::Matrix4.
[[nodiscard]] inline ktm::fmat4x4 look_at_lh(const ktm::fvec3& eye, const ktm::fvec3& target, const ktm::fvec3& up) noexcept {
    const ktm::fvec3 forward = ktm::normalize(target - eye);
    const ktm::fvec3 right = ktm::normalize(cross_lh(up, forward));
    const ktm::fvec3 view_up = cross_lh(forward, right);
    return ktm::fmat4x4{
        ktm::fvec4{right[0], view_up[0], forward[0], 0.0f},
        ktm::fvec4{right[1], view_up[1], forward[1], 0.0f},
        ktm::fvec4{right[2], view_up[2], forward[2], 0.0f},
        ktm::fvec4{-ktm::dot(right, eye), -ktm::dot(view_up, eye), -ktm::dot(forward, eye), 1.0f}};
}

// ktm's left-hand projection is +Y up. Vulkan NDC is +Y down, so negate Y.
[[nodiscard]] inline ktm::fmat4x4 perspective_lh(float fov_radians, float aspect, float znear, float zfar) noexcept {
    ktm::fmat4x4 projection = ktm::perspective_lh(fov_radians, aspect, znear, zfar);
    projection[1][1] = -projection[1][1];
    return projection;
}

// bottom/top follow the previous ortho() argument order. ktm::ortho_lh takes top then bottom.
[[nodiscard]] inline ktm::fmat4x4 ortho_lh(float left, float right, float bottom, float top, float znear, float zfar) noexcept {
    ktm::fmat4x4 projection = ktm::ortho_lh(left, right, top, bottom, znear, zfar);
    projection[1][1] = -projection[1][1];
    return projection;
}

[[nodiscard]] inline const float* matrix_data(const ktm::fmat4x4& matrix) noexcept {
    static_assert(sizeof(ktm::fmat4x4) == sizeof(float) * 16);
    return reinterpret_cast<const float*>(&matrix);
}

}// namespace ocarina
