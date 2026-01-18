//
// Created by Z on 2026/1/18.
//

#include "texture.h"

namespace ocarina {

Texture::Texture(Device::Impl *device, ocarina::PixelStorage pixel_storage,
                 RHIResource::Tag tag, ocarina::handle_ty handle)
    : RHIResource(device, tag, handle) {
}

}// namespace ocarina