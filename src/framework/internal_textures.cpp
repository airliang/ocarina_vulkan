#include "internal_textures.h"
#include "resource_manager.h"
#include "rhi/device.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"

namespace ocarina {

InternalTextures& InternalTextures::instance() {
    static InternalTextures s_instance;
    return s_instance;
}

Texture* InternalTextures::get_white_texture(Device* device) {
    if (white_texture_ != nullptr && device_ == device) {
        return white_texture_;
    }

    TextureViewCreation texture_view{};
    texture_view.mip_level_count = 1;
    texture_view.usage = TextureUsageFlags::ShaderReadOnly;
    TextureSampler sampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};

    static const uint8_t white_pixels[4 * 4 * 4] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    };

    white_texture_ = ResourceManager::instance().create_texture(
        device,
        "__internal_white__",
        4,
        4,
        PixelStorage::BYTE4,
        texture_view,
        sampler,
        white_pixels);
    device_ = device;
    return white_texture_;
}

void InternalTextures::cleanup() {
    white_texture_ = nullptr;
    device_ = nullptr;
}

}// namespace ocarina
