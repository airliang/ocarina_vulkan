#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "material.h"

namespace ocarina {

class Device;
class Texture;

class OC_FRAMEWORK_API InternalTextures : public concepts::Noncopyable {
public:
    static InternalTextures& instance();

    [[nodiscard]] Material::TextureHandle get_white_texture_handle(Device* device);
    /// May return nullptr until the GPU resource thread finishes creating the texture.
    [[nodiscard]] Texture* get_white_texture(Device* device);
    void cleanup();

private:
    InternalTextures() = default;

    Material::TextureHandle white_handle_{};
    Device* device_ = nullptr;
};

}// namespace ocarina
