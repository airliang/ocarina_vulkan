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

Material::TextureHandle InternalTextures::get_white_texture_handle(Device* device) {
    TextureViewCreation texture_view{};
    texture_view.mip_level_count = 1;
    texture_view.usage = TextureUsageFlags::ShaderReadOnly;
    TextureSampler sampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};

    if (white_handle_.bindless_index_ != InvalidUI32 && device_ == device) {
        if (white_handle_.texture_ == nullptr) {
            white_handle_ = ResourceManager::instance().get_texture_handle(
                "__internal_white__",
                texture_view,
                sampler);
        }
        return white_handle_;
    }

    static const uint8_t white_pixels[4 * 4 * 4] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    };

    white_handle_ = ResourceManager::instance().create_texture(
        device,
        "__internal_white__",
        4,
        4,
        PixelStorage::BYTE4,
        texture_view,
        sampler,
        white_pixels);
    device_ = device;
    return white_handle_;
}

Texture* InternalTextures::get_white_texture(Device* device) {
    return get_white_texture_handle(device).texture_;
}

void InternalTextures::cleanup() {
    white_handle_ = {};
    device_ = nullptr;
}

}// namespace ocarina
