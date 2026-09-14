#include "cubemap.h"
#include "rhi/device.h"
#include "core/image.h"
#include "core/logging.h"

namespace ocarina {

Cubemap::Cubemap(
    Device::Impl *device,
    const Image (&faces)[6],
    const TextureSampler &sampler)
    : RHIResource(device, Tag::CUBEMAP, 0) {
    OC_ASSERT(device != nullptr);
    for (uint32_t i = 1; i < 6; ++i) {
        if (faces[i].resolution().x != faces[0].resolution().x
            || faces[i].resolution().y != faces[0].resolution().y
            || faces[i].pixel_storage() != faces[0].pixel_storage()) {
            OC_ERROR("Cubemap faces must share the same resolution and pixel format");
            return;
        }
    }
    handle_ = device->create_cubemap(
        faces[0].resolution().x,
        faces[0].resolution().y,
        faces[0].pixel_storage(),
        sampler);
}

}// namespace ocarina
