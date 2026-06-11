//
// Created by Zero on 06/06/2022.
//

#include "device.h"
#include "resources/texture.h"
#include "context.h"
#include "core/dynamic_module.h"

namespace ocarina {

Texture Device::create_texture(uint3 res, PixelStorage storage, const string &desc) const noexcept {
    return create<Texture>(res, storage, 1, desc);
}

Texture Device::create_texture(uint2 res, PixelStorage storage, const string &desc) const noexcept {
    return create_texture(make_uint3(res, 1u), storage, desc);
}

Texture Device::create_texture(Image *image_resource, const TextureViewCreation &texture_view, const TextureSampler& sampler) const noexcept {
    Texture tex(impl_.get(), image_resource, texture_view, sampler);
    return tex;
}

Texture Device::create_texture(uint32_t width, uint32_t height, uint32_t depth, PixelStorage pixel_storage,
                               const TextureViewCreation &texture_view, const TextureSampler& sampler,
                               uint4 default_color, const void *data) const noexcept {
    return Texture(impl_.get(), width, height, depth, pixel_storage, texture_view, sampler, default_color, data);
}

Device Device::create_device(const string &backend_name, const ocarina::InstanceCreation &instance_creation) {
    RHIContext &rhi_context = RHIContext::instance();
    return rhi_context.create_device(backend_name, instance_creation);
}

Device Device::create_device(const string &backend_name) {
    RHIContext &rhi_context = RHIContext::instance();
    return rhi_context.create_device(backend_name);
}

}// namespace ocarina
