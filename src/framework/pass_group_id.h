#pragma once

#include "core/header.h"
#include "core/stl.h"
#include <cstdint>
#include <optional>
#include <string_view>

namespace ocarina {

/// Logical recording / scheduling groups. Numeric order is the default frame
/// execution order when iterating Renderer's pass-group map.
enum class PassGroupId : uint32_t {
    ComputePass = 0,
    Offscreen,
    GBuffer,
    Lighting,
    DepthPrepass,
    Skybox,
    Opaque,
    Transparent,
    PostProcess,
    UI,
    Count
};

[[nodiscard]] inline const char* pass_group_id_name(PassGroupId id) noexcept {
    switch (id) {
        case PassGroupId::ComputePass: return "ComputePass";
        case PassGroupId::Offscreen: return "Offscreen";
        case PassGroupId::GBuffer: return "GBuffer";
        case PassGroupId::Lighting: return "Lighting";
        case PassGroupId::DepthPrepass: return "DepthPrepass";
        case PassGroupId::Skybox: return "Skybox";
        case PassGroupId::Opaque: return "Opaque";
        case PassGroupId::Transparent: return "Transparent";
        case PassGroupId::PostProcess: return "PostProcess";
        case PassGroupId::UI: return "UI";
        default: return "Unknown";
    }
}

[[nodiscard]] inline std::optional<PassGroupId> pass_group_id_from_name(std::string_view name) noexcept {
    for (uint32_t i = 0; i < static_cast<uint32_t>(PassGroupId::Count); ++i) {
        const auto id = static_cast<PassGroupId>(i);
        if (name == pass_group_id_name(id)) {
            return id;
        }
    }
    return std::nullopt;
}

}// namespace ocarina
