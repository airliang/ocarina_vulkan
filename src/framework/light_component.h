#pragma once

#include "math/basic_types.h"

namespace ocarina {

enum class LightType : uint8_t {
    Directional = 0,
    Point,
    Spot
};

/// Linear RGB color (0–1 range typical for light tint).
using Color = float3;

struct LightComponent {
    LightType type = LightType::Directional;

    float intensity = 0.0f;

    /// Attenuation range for point/spot lights (world units).
    float range = 10.0f;

    /// Soft radius / source size (point/spot); unused for directional by default.
    float radius = 0.0f;

    Color color = make_float3(1.0f, 1.0f, 1.0f);

    bool cast_shadow = false;

    [[nodiscard]] bool is_active() const noexcept {
        return intensity > 0.0f;
    }
};

}// namespace ocarina
