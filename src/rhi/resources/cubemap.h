#pragma once

#include "core/header.h"
#include "resource.h"
#include "texture_sampler.h"
#include "../graphics_descriptions.h"

namespace ocarina {

class Image;

/// GPU cube map (6 faces). Constructed from six equally sized images; CPU→GPU upload
/// is performed by the framework StagingUploader / GPUResourceThread.
class OC_RHI_API Cubemap : public RHIResource {
public:
    class Impl {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual uint2 face_resolution() const noexcept = 0;
        [[nodiscard]] virtual PixelStorage pixel_storage() const noexcept = 0;
        [[nodiscard]] virtual uint32_t mip_levels() const noexcept = 0;
        [[nodiscard]] virtual handle_ty image_handle() const noexcept = 0;
        [[nodiscard]] virtual const TextureSampler *get_sampler_pointer() const noexcept = 0;
        [[nodiscard]] virtual const void *handle_ptr() const noexcept = 0;
    };

    Cubemap() = default;

    /// @param faces Six cube faces in Vulkan order: +X,-X,+Y,-Y,+Z,-Z.
    explicit Cubemap(
        Device::Impl *device,
        const Image (&faces)[6],
        const TextureSampler &sampler);

    [[nodiscard]] bool is_gpu_ready() const noexcept {
        return gpu_resource_state_ >= GPUResourceState::GPU_Ready;
    }

    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }
    [[nodiscard]] Impl *operator->() noexcept { return impl(); }
    [[nodiscard]] const Impl *operator->() const noexcept { return impl(); }

    [[nodiscard]] uint2 face_resolution() const noexcept { return impl()->face_resolution(); }
    [[nodiscard]] PixelStorage pixel_storage() const noexcept { return impl()->pixel_storage(); }
    [[nodiscard]] uint32_t mip_levels() const noexcept { return impl()->mip_levels(); }
    [[nodiscard]] handle_ty image_handle() const noexcept { return impl()->image_handle(); }
    [[nodiscard]] const TextureSampler *get_sampler_pointer() const noexcept {
        return impl()->get_sampler_pointer();
    }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return impl()->handle_ptr(); }
};

}// namespace ocarina
