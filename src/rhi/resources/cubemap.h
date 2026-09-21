#pragma once

#include "core/header.h"
#include "resource.h"
#include "texture_sampler.h"
#include "../graphics_descriptions.h"

namespace ocarina {

class Image;

[[nodiscard]] constexpr TextureUsageFlags cubemap_sampled_upload_usage() noexcept {
    return static_cast<TextureUsageFlags>(
        static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)
        | static_cast<uint32_t>(TextureUsageFlags::CopyDst));
}

[[nodiscard]] constexpr TextureUsageFlags cubemap_storage_usage() noexcept {
    return static_cast<TextureUsageFlags>(
        static_cast<uint32_t>(TextureUsageFlags::ShaderReadWrite)
        | static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly));
}

/// Full mip chain for a square (or rectangular) cube face.
[[nodiscard]] constexpr uint32_t cubemap_mip_count(uint32_t width, uint32_t height) noexcept {
    uint32_t size = width > height ? width : height;
    uint32_t levels = 1;
    while (size > 1u) {
        size >>= 1u;
        ++levels;
    }
    return levels;
}

/// GPU cube map (6 faces). Empty allocations are created immediately; CPU face
/// upload is performed by StagingUploader / GPUResourceThread.
class OC_RHI_API Cubemap : public RHIResource {
public:
    class Impl {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual uint2 face_resolution() const noexcept = 0;
        [[nodiscard]] virtual PixelStorage pixel_storage() const noexcept = 0;
        [[nodiscard]] virtual uint32_t mip_levels() const noexcept = 0;
        [[nodiscard]] virtual TextureUsageFlags usage_flags() const noexcept = 0;
        [[nodiscard]] virtual handle_ty image_handle() const noexcept = 0;
        [[nodiscard]] virtual const TextureSampler *get_sampler_pointer() const noexcept = 0;
        [[nodiscard]] virtual const void *handle_ptr() const noexcept = 0;
    };

    Cubemap() = default;

    /// Empty cube image (no pixel upload). Use CopyDst usage to fill faces later.
    /// @p mip_levels 0 = full chain, 1 = base level only.
    explicit Cubemap(
        Device::Impl *device,
        uint32_t width,
        uint32_t height,
        PixelStorage pixel_storage,
        const TextureSampler &sampler,
        TextureUsageFlags usage,
        uint32_t mip_levels = 1);

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
    [[nodiscard]] TextureUsageFlags usage_flags() const noexcept { return impl()->usage_flags(); }
    [[nodiscard]] handle_ty image_handle() const noexcept { return impl()->image_handle(); }
    [[nodiscard]] const TextureSampler *get_sampler_pointer() const noexcept {
        return impl()->get_sampler_pointer();
    }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return impl()->handle_ptr(); }
};

}// namespace ocarina
