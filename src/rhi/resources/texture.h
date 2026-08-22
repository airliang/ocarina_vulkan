//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "resource.h"
#include "texture_sampler.h"
#include "../graphics_descriptions.h"

namespace ocarina {

class Image;
namespace detail {
[[nodiscard]] constexpr uint compute_mip_level_num(uint3 res, uint request_level_num) noexcept {
    uint max_size = std::max({res.x, res.y, res.z});
    auto max_levels = 0u;
    while (max_size != 0u) {
        max_size >>= 1u;
        max_levels++;
    }
    return request_level_num == 0 ? max_levels : std::min(request_level_num, max_levels);
}
}// namespace detail

class OC_RHI_API Texture : public RHIResource {
protected:
    uint channel_num_{};

public:
    class Impl {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual uint3 resolution() const noexcept = 0;
        [[nodiscard]] virtual PixelStorage pixel_storage() const noexcept = 0;
        [[nodiscard]] virtual uint32_t mip_levels() const noexcept = 0;
        [[nodiscard]] virtual handle_ty array_handle() const noexcept = 0;
        [[nodiscard]] virtual const handle_ty *array_handle_ptr() const noexcept = 0;
        [[nodiscard]] virtual handle_ty tex_handle() const noexcept = 0;
        [[nodiscard]] virtual const TextureSampler *get_sampler_pointer() const noexcept = 0;
        [[nodiscard]] virtual const void *handle_ptr() const noexcept = 0;
        [[nodiscard]] virtual size_t data_size() const noexcept = 0;
        [[nodiscard]] virtual size_t data_alignment() const noexcept = 0;
        [[nodiscard]] virtual size_t max_member_size() const noexcept = 0;
        [[nodiscard]] virtual bool is_render_target() const noexcept { return false; }
        [[nodiscard]] virtual TextureUsageFlags usage_flags() const noexcept { return TextureUsageFlags::None; }
    };

public:
    Texture() = default;
    explicit Texture(Device::Impl *device, uint3 res,
                     PixelStorage pixel_storage, uint level_num = 1u,
                     const string &desc = "");

    explicit Texture(
        Device::Impl *device,
        Image *image_resource,
        const TextureViewCreation &texture_view,
        const TextureSampler &sampler);

    explicit Texture(
        Device::Impl *device,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        PixelStorage pixel_storage,
        const TextureViewCreation &texture_view,
        const TextureSampler &sampler,
        uint4 default_color,
        const void *data);

    explicit Texture(Device::Impl *device, uint32_t width, uint32_t height, PixelStorage pixel_storage, TextureUsageFlags usage);

    /// Upload base-level pixels (generates CPU mip chain when mip_levels > 1).
    void upload_pixels(const void *data, size_t base_level_bytes);

    void load_cpu_data(const void *data, size_t size_in_bytes);
    void load_cpu_data(Image *image);

    /// Staging upload into an existing backend texture impl (used by Texture and drivers).
    static void upload_cpu_pixels(
        Device::Impl *device,
        Impl *texture,
        const void *data,
        size_t base_level_bytes);

    /// True when the GPU upload has finished (GPU_Ready).
    [[nodiscard]] bool is_gpu_ready() const noexcept {
        return gpu_resource_state_ >= GPUResourceState::GPU_Ready;
    }

    OC_MAKE_MEMBER_GETTER(channel_num, )

    [[nodiscard]] uint pixel_num() const noexcept {
        uint3 res = impl()->resolution();
        return res.x * res.y * res.z;
    }

    [[nodiscard]] uint size_in_byte() const noexcept {
        return pixel_num() * pixel_size();
    }

    [[nodiscard]] uint pixel_size() const noexcept {
        return ::ocarina::pixel_size(impl()->pixel_storage());
    }

    [[nodiscard]] uint3 resolution() const noexcept {
        return impl()->resolution();
    }

    [[nodiscard]] uint32_t mip_levels() const noexcept {
        return impl()->mip_levels();
    }

    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }
    [[nodiscard]] Impl *operator->() noexcept { return impl(); }
    [[nodiscard]] const Impl *operator->() const noexcept { return impl(); }
    [[nodiscard]] handle_ty array_handle() const noexcept { return impl()->array_handle(); }
    [[nodiscard]] handle_ty tex_handle() const noexcept { return impl()->tex_handle(); }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return impl()->handle_ptr(); }
    [[nodiscard]] size_t data_size() const noexcept override { return impl()->data_size(); }
    [[nodiscard]] size_t data_alignment() const noexcept override { return impl()->data_alignment(); }
    [[nodiscard]] const TextureSampler *get_sampler_pointer() const noexcept {
        return impl()->get_sampler_pointer();
    }

    [[nodiscard]] bool is_render_target() const noexcept { return impl()->is_render_target(); }
    [[nodiscard]] TextureUsageFlags usage_flags() const noexcept { return impl()->usage_flags(); }
};

template<typename T>
class Texture2D : public Texture {
public:
    using Super = Texture;
    static constexpr auto Dim = vector_dimension_v<T>;

public:
    Texture2D() = default;
    explicit Texture2D(Device::Impl *device, uint2 res,
                       PixelStorage pixel_storage, uint level_num = 1u,
                       const string &desc = "")
        : Texture(device, make_uint3(res, 1u), pixel_storage, level_num, desc) {}
};

}// namespace ocarina
