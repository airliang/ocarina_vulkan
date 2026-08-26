#include "staging_uploader.h"
#include "rhi/command_buffer.h"
#include "rhi/fence.h"
#include "rhi/resources/buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/vertex_buffer.h"
#include "rhi/resources/texture.h"
#include "core/image_base.h"
#include "core/logging.h"
#include "core/profiler.h"

#include "stb_image_resize.h"

namespace ocarina {

namespace {

struct CpuMipChain {
    std::vector<uint8_t> pixels;
    std::vector<uint32_t> level_offsets;
    std::vector<uint32_t> level_widths;
    std::vector<uint32_t> level_heights;
};

size_t mip_level_byte_size(uint32_t width, uint32_t height, uint32_t channels) {
    return static_cast<size_t>(width) * height * channels;
}

bool resize_mip_level(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h,
    uint32_t channels,
    PixelStorage format) {
    if (format == PixelStorage::BYTE4) {
        return stbir_resize_uint8_srgb(
                   src, static_cast<int>(src_w), static_cast<int>(src_h), 0,
                   dst, static_cast<int>(dst_w), static_cast<int>(dst_h), 0,
                   static_cast<int>(channels), 3, 0) != 0;
    }
    if (is_8bit(format)) {
        return stbir_resize_uint8(
                   src, static_cast<int>(src_w), static_cast<int>(src_h), 0,
                   dst, static_cast<int>(dst_w), static_cast<int>(dst_h), 0,
                   static_cast<int>(channels)) != 0;
    }
    return false;
}

CpuMipChain build_cpu_mip_chain(
    const void *base_pixels,
    uint32_t width,
    uint32_t height,
    uint32_t mip_levels,
    PixelStorage format) {
    PROFILE_SCOPE();
    const uint32_t channels = static_cast<uint32_t>(channel_num(format));
    const uint8_t *base = static_cast<const uint8_t *>(base_pixels);

    CpuMipChain chain;
    chain.level_offsets.reserve(mip_levels);
    chain.level_widths.reserve(mip_levels);
    chain.level_heights.reserve(mip_levels);

    size_t total_bytes = 0;
    uint32_t level_w = width;
    uint32_t level_h = height;
    for (uint32_t i = 0; i < mip_levels; ++i) {
        const uint32_t mip_w = std::max(level_w, 1u);
        const uint32_t mip_h = std::max(level_h, 1u);
        chain.level_offsets.push_back(static_cast<uint32_t>(total_bytes));
        chain.level_widths.push_back(mip_w);
        chain.level_heights.push_back(mip_h);
        total_bytes += mip_level_byte_size(mip_w, mip_h, channels);
        if (level_w > 1) {
            level_w /= 2;
        }
        if (level_h > 1) {
            level_h /= 2;
        }
    }

    chain.pixels.resize(total_bytes);
    std::memcpy(chain.pixels.data(), base, mip_level_byte_size(width, height, channels));

    uint32_t src_w = width;
    uint32_t src_h = height;
    for (uint32_t i = 1; i < mip_levels; ++i) {
        const uint32_t dst_w = chain.level_widths[i];
        const uint32_t dst_h = chain.level_heights[i];
        const uint8_t *src = chain.pixels.data() + chain.level_offsets[i - 1];
        uint8_t *dst = chain.pixels.data() + chain.level_offsets[i];
        if (!resize_mip_level(src, src_w, src_h, dst, dst_w, dst_h, channels, format)) {
            throw std::runtime_error("failed to generate CPU mipmaps with stb_image_resize");
        }
        src_w = dst_w;
        src_h = dst_h;
    }

    return chain;
}

}// namespace

StagingUploader::StagingUploader(Device::Impl *device)
    : device_(device) {
    OC_ASSERT(device_ != nullptr);
}

StagingUploader::~StagingUploader() {
    destroy_staging();
}

void StagingUploader::destroy_staging() noexcept {
    if (device_ == nullptr || staging_ == 0) {
        staging_ = 0;
        capacity_ = 0;
        return;
    }
    device_->destroy_buffer(staging_);
    staging_ = 0;
    capacity_ = 0;
}

void StagingUploader::ensure_capacity(size_t required_bytes) {
    if (required_bytes == 0 || required_bytes <= capacity_) {
        return;
    }

    size_t new_capacity = capacity_ == 0 ? required_bytes : capacity_;
    while (new_capacity < required_bytes) {
        const size_t next = new_capacity > (std::numeric_limits<size_t>::max() / 2)
                                ? required_bytes
                                : new_capacity * 2;
        new_capacity = std::max(next, required_bytes);
    }

    destroy_staging();

    staging_ = device_->create_buffer(
        new_capacity,
        GraphicBufferBindFlags::CopySrc,
        "staging_uploader");
    if (staging_ == 0) {
        capacity_ = 0;
        return;
    }
    capacity_ = new_capacity;
}

void StagingUploader::write_staging(const void *data, size_t size_in_byte) {
    Buffer *staging = reinterpret_cast<Buffer *>(staging_);
    OC_ASSERT(staging != nullptr);
    OC_ASSERT(size_in_byte <= capacity_);

    const auto *src = static_cast<const uint8_t *>(data);
    size_t offset = 0;
    while (offset < size_in_byte) {
        const size_t remaining = size_in_byte - offset;
        const uint32_t chunk = static_cast<uint32_t>(std::min(
            remaining,
            static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
        staging->copy_from_immediately(src + offset, chunk, static_cast<uint32_t>(offset));
        offset += chunk;
    }
}

void StagingUploader::upload_to_buffer(
    handle_ty dst_buffer,
    const void *data,
    size_t size_in_byte,
    size_t dst_offset) {
    if (device_ == nullptr || dst_buffer == 0 || data == nullptr || size_in_byte == 0) {
        return;
    }

    ensure_capacity(size_in_byte);
    if (staging_ == 0) {
        return;
    }

    write_staging(data, size_in_byte);

    CommandBuffer cmd = device_->get_command_buffer(QueueType::Copy);
    cmd.begin();
    cmd.copy_buffer(staging_, dst_buffer, 0, dst_offset, size_in_byte);
    cmd.end();

    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device_->release_command_buffer(cmd);
}

void StagingUploader::upload_to_texture(
    handle_ty dst_texture,
    const void *data,
    size_t size_in_byte,
    const BufferTextureCopy *regions,
    uint32_t region_count) {
    if (device_ == nullptr || dst_texture == 0 || data == nullptr || size_in_byte == 0
        || regions == nullptr || region_count == 0) {
        return;
    }

    ensure_capacity(size_in_byte);
    if (staging_ == 0) {
        return;
    }

    write_staging(data, size_in_byte);

    CommandBuffer cmd = device_->get_command_buffer(QueueType::Copy);
    cmd.begin();
    cmd.transition_texture_layout(
        dst_texture,
        TextureLayout::Undefined,
        TextureLayout::TransferDst);
    cmd.copy_buffer_to_texture(staging_, dst_texture, regions, region_count);
    cmd.transition_texture_layout(
        dst_texture,
        TextureLayout::TransferDst,
        TextureLayout::ShaderReadOnly);
    cmd.end();

    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device_->release_command_buffer(cmd);
}

void StagingUploader::upload_index_buffer_range(
    IndexBuffer *index_buffer,
    const void *data,
    uint32_t index_offset,
    uint32_t index_count) {
    if (index_buffer == nullptr || data == nullptr || index_count == 0) {
        return;
    }
    OC_ASSERT(index_buffer->buffer() != nullptr);

    const uint32_t stride = index_buffer->is_16_bit() ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint64_t num_bytes = static_cast<uint64_t>(index_count) * stride;
    const uint64_t dst_offset = static_cast<uint64_t>(index_offset) * stride;
    upload_to_buffer(
        index_buffer->buffer_handle(),
        data,
        num_bytes,
        dst_offset);
}

void StagingUploader::upload_vertex_attribute_range(
    VertexBuffer *vertex_buffer,
    VertexAttributeType::Enum type,
    const void *data,
    uint32_t vertex_offset,
    uint32_t vertex_count) {
    if (vertex_buffer == nullptr || data == nullptr || vertex_count == 0) {
        return;
    }

    VertexStream *stream = vertex_buffer->get_vertex_stream(type);
    if (stream == nullptr || stream->buffer == nullptr || stream->stride == 0) {
        return;
    }
    OC_ASSERT(vertex_offset + vertex_count <= stream->count);

    const uint64_t byte_size = static_cast<uint64_t>(vertex_count) * stream->stride;
    const uint64_t dst_offset = static_cast<uint64_t>(vertex_offset) * stream->stride;
    upload_to_buffer(
        reinterpret_cast<handle_ty>(stream->buffer),
        data,
        byte_size,
        dst_offset);
}

void StagingUploader::upload_texture_cpu_pixels(
    Texture *texture,
    const void *data,
    size_t base_level_bytes) {
    PROFILE_SCOPE();
    if (texture == nullptr || texture->impl() == nullptr || data == nullptr || base_level_bytes == 0) {
        return;
    }

    Texture::Impl *impl = texture->impl();
    const uint32_t mip_levels = impl->mip_levels();
    const PixelStorage format = impl->pixel_storage();
    const uint3 res = impl->resolution();

    if (mip_levels <= 1) {
        BufferTextureCopy region{};
        region.buffer_offset = 0;
        region.mip_level = 0;
        region.width = res.x;
        region.height = res.y;
        region.depth = std::max(res.z, 1u);
        region.layer_count = 1;
        upload_to_texture(
            reinterpret_cast<handle_ty>(impl),
            data,
            base_level_bytes,
            &region,
            1);
        return;
    }

    if (!is_8bit(format)) {
        throw std::runtime_error("CPU mipmap generation only supports 8-bit pixel formats");
    }

    const CpuMipChain chain = build_cpu_mip_chain(data, res.x, res.y, mip_levels, format);

    std::vector<BufferTextureCopy> regions(chain.level_offsets.size());
    for (size_t i = 0; i < chain.level_offsets.size(); ++i) {
        BufferTextureCopy &region = regions[i];
        region.buffer_offset = chain.level_offsets[i];
        region.mip_level = static_cast<uint32_t>(i);
        region.width = chain.level_widths[i];
        region.height = chain.level_heights[i];
        region.depth = 1;
        region.layer_count = 1;
    }
    upload_to_texture(
        reinterpret_cast<handle_ty>(impl),
        chain.pixels.data(),
        chain.pixels.size(),
        regions.data(),
        static_cast<uint32_t>(regions.size()));
}

}// namespace ocarina
