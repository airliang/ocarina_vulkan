//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"

namespace ocarina {

class Texture;

/// A render pass always targets a RenderTarget: either the swapchain backbuffer
/// or one or more offscreen color textures (+ optional depth).
class OC_RHI_API RenderTarget : public concepts::Noncopyable {
public:
    enum class Type : uint8_t {
        Swapchain = 0,
        Texture,
    };

    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;

    /// Swapchain color (+ swapchain depth/stencil managed by the device).
    [[nodiscard]] static RenderTarget swapchain() noexcept;

    /// Single offscreen color texture (+ optional depth texture).
    [[nodiscard]] static RenderTarget texture(Texture *color, Texture *depth = nullptr) noexcept;

    /// Multiple offscreen color textures (+ optional depth texture).
    [[nodiscard]] static RenderTarget texture(
        Texture *const *colors,
        uint32_t color_count,
        Texture *depth = nullptr) noexcept;

    RenderTarget() noexcept = default;
    RenderTarget(RenderTarget &&other) noexcept;
    RenderTarget &operator=(RenderTarget &&other) noexcept;

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_swapchain() const noexcept { return type_ == Type::Swapchain; }
    [[nodiscard]] bool is_texture() const noexcept { return type_ == Type::Texture; }

    [[nodiscard]] uint32_t color_attachment_count() const noexcept { return color_attachment_count_; }
    [[nodiscard]] Texture *color_attachment(uint32_t index) const noexcept {
        return index < color_attachment_count_ ? color_attachments_[index] : nullptr;
    }
    [[nodiscard]] Texture *depth_attachment() const noexcept { return depth_attachment_; }

    void set_name(std::string name) { name_ = std::move(name); }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

private:
    Type type_ = Type::Swapchain;
    uint32_t color_attachment_count_ = 0;
    Texture *color_attachments_[MAX_COLOR_ATTACHMENTS] = {};
    Texture *depth_attachment_ = nullptr;
    std::string name_ = "RenderTarget";
};

}// namespace ocarina
