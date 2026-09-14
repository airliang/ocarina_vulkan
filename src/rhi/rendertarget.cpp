#include "rendertarget.h"
#include "resources/texture.h"
#include "core/logging.h"

namespace ocarina {

RenderTarget RenderTarget::swapchain() noexcept {
    RenderTarget target;
    target.type_ = Type::Swapchain;
    target.color_attachment_count_ = 0;
    target.depth_attachment_ = nullptr;
    target.name_ = "Swapchain";
    return target;
}

RenderTarget RenderTarget::texture(Texture *color, Texture *depth) noexcept {
    Texture *colors[1] = {color};
    return texture(colors, color != nullptr ? 1u : 0u, depth);
}

RenderTarget RenderTarget::texture(
    Texture *const *colors,
    uint32_t color_count,
    Texture *depth) noexcept {
    RenderTarget target;
    target.type_ = Type::Texture;
    target.depth_attachment_ = depth;
    target.name_ = "Texture";

    if (colors == nullptr || color_count == 0) {
        OC_ASSERT(depth != nullptr);
        return target;
    }

    OC_ASSERT(color_count <= MAX_COLOR_ATTACHMENTS);
    target.color_attachment_count_ = color_count;
    for (uint32_t i = 0; i < color_count; ++i) {
        target.color_attachments_[i] = colors[i];
    }
    return target;
}

RenderTarget::RenderTarget(RenderTarget &&other) noexcept {
    type_ = other.type_;
    color_attachment_count_ = other.color_attachment_count_;
    depth_attachment_ = other.depth_attachment_;
    name_ = std::move(other.name_);
    for (uint32_t i = 0; i < MAX_COLOR_ATTACHMENTS; ++i) {
        color_attachments_[i] = other.color_attachments_[i];
        other.color_attachments_[i] = nullptr;
    }
    other.type_ = Type::Swapchain;
    other.color_attachment_count_ = 0;
    other.depth_attachment_ = nullptr;
}

RenderTarget &RenderTarget::operator=(RenderTarget &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    type_ = other.type_;
    color_attachment_count_ = other.color_attachment_count_;
    depth_attachment_ = other.depth_attachment_;
    name_ = std::move(other.name_);
    for (uint32_t i = 0; i < MAX_COLOR_ATTACHMENTS; ++i) {
        color_attachments_[i] = other.color_attachments_[i];
        other.color_attachments_[i] = nullptr;
    }
    other.type_ = Type::Swapchain;
    other.color_attachment_count_ = 0;
    other.depth_attachment_ = nullptr;
    return *this;
}

}// namespace ocarina
