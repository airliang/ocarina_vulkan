#include "texture.h"
#include "rhi/device.h"
#include "core/image.h"
#include "core/image_base.h"

namespace ocarina {

Texture::Texture(
    Device::Impl *device,
    uint3 res,
    PixelStorage pixel_storage,
    uint level_num,
    const string &desc)
    : RHIResource(device, Tag::TEXTURE,
                  device->create_texture(res, pixel_storage,
                                         detail::compute_mip_level_num(res, level_num), desc)),
      channel_num_(ocarina::channel_num(pixel_storage)) {}

Texture::Texture(
    Device::Impl *device,
    Image *image_resource,
    const TextureViewCreation &texture_view,
    const TextureSampler &sampler)
    : RHIResource(device, Tag::TEXTURE, 0),
      channel_num_(ocarina::channel_num(texture_view.format)) {
    // Backend allocates the GPU image only; CPU→GPU upload is a framework concern.
    handle_ = device->create_texture(image_resource, texture_view, sampler);
    (void)image_resource;
}

Texture::Texture(
    Device::Impl *device,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    PixelStorage pixel_storage,
    const TextureViewCreation &texture_view,
    const TextureSampler &sampler,
    uint4 default_color,
    const void *data)
    : RHIResource(device, Tag::TEXTURE, 0),
      channel_num_(ocarina::channel_num(pixel_storage)) {
    (void)default_color;
    (void)data;
    // Backend allocates the GPU image only; callers upload via framework StagingUploader.
    handle_ = device->create_texture(
        width,
        height,
        depth,
        pixel_storage,
        texture_view,
        sampler,
        default_color,
        nullptr);
}

Texture::Texture(
    Device::Impl *device,
    uint32_t width,
    uint32_t height,
    PixelStorage pixel_storage,
    TextureUsageFlags usage)
    : RHIResource(device, Tag::TEXTURE, device->create_render_target_texture(width, height, pixel_storage, usage)),
      channel_num_(ocarina::channel_num(pixel_storage)) {}

}// namespace ocarina
