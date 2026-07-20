#pragma once

#include "core/header.h"
#include <cstdint>

namespace ocarina {

/// Logical recording / scheduling groups. Numeric order is the default frame
/// execution order when iterating Renderer's pass-group map.
enum class PassGroupId : uint32_t {
    Shadow = 0,
    Offscreen,
    GBuffer,
    Lighting,
    Opaque,
    Transparent,
    PostProcess,
    UI,
    Count
};

[[nodiscard]] inline const char* pass_group_id_name(PassGroupId id) noexcept {
    switch (id) {
        case PassGroupId::Shadow: return "Shadow";
        case PassGroupId::Offscreen: return "Offscreen";
        case PassGroupId::GBuffer: return "GBuffer";
        case PassGroupId::Lighting: return "Lighting";
        case PassGroupId::Opaque: return "Opaque";
        case PassGroupId::Transparent: return "Transparent";
        case PassGroupId::PostProcess: return "PostProcess";
        case PassGroupId::UI: return "UI";
        default: return "Unknown";
    }
}

}// namespace ocarina
