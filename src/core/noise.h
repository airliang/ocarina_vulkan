#pragma once

#include "header.h"
#include "math/basic_types.h"

namespace ocarina {

/// Classic improved Perlin noise (Ken Perlin). Output roughly in [-1, 1].
[[nodiscard]] OC_CORE_API float perlin_noise_2d(float x, float y) noexcept;
[[nodiscard]] OC_CORE_API float perlin_noise_2d(float2 p) noexcept;

[[nodiscard]] OC_CORE_API float perlin_noise_3d(float x, float y, float z) noexcept;
[[nodiscard]] OC_CORE_API float perlin_noise_3d(float3 p) noexcept;

/// Fractal Brownian motion: sum of @p octaves of Perlin noise.
[[nodiscard]] OC_CORE_API float perlin_fbm_2d(
    float2 p,
    uint32_t octaves = 4,
    float lacunarity = 2.0f,
    float gain = 0.5f) noexcept;

[[nodiscard]] OC_CORE_API float perlin_fbm_3d(
    float3 p,
    uint32_t octaves = 4,
    float lacunarity = 2.0f,
    float gain = 0.5f) noexcept;

}// namespace ocarina
