#pragma once

#include "core/header.h"
#include "math/basic_types.h"
#include "math.h"

namespace ocarina {

/// CPU mirror of `global_ubo` in `res/shaderlibrary/builtin/common.hlsl`.
/// Matrices are stored transposed for HLSL row-major upload (same as previous tests).
struct alignas(16) GlobalUniformBuffer {
    math3d::Matrix4 projection_matrix{};
    math3d::Matrix4 view_matrix{};
    float4 camera_pos = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 light_pos = make_float4(5.0f, 10.0f, 5.0f, 1.0f);
    float4 sun_direction = make_float4(-0.4f, -1.0f, -0.3f, 0.0f);
    float4 sun_color = make_float4(1.0f, 1.0f, 1.0f, 1.0f);
    float sun_intensity = 10.0f;
    float sun_pad0 = 0.0f;
    float sun_pad1 = 0.0f;
    float sun_pad2 = 0.0f;
};

}// namespace ocarina
