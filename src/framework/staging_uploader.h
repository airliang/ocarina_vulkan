#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/device.h"
#include "rhi/vertex_buffer.h"

namespace ocarina {

class IndexBuffer;
class VertexBuffer;
class Texture;

/// Reusable host-visible staging buffer for CPU → GPU copies.
/// Owned by GPUResourceThread (single-threaded upload path).
class OC_FRAMEWORK_API StagingUploader : public concepts::Noncopyable {
public:
    explicit StagingUploader(Device::Impl *device);
    ~StagingUploader();

    /// Low-level: copy into a device-local buffer handle.
    void upload_to_buffer(
        handle_ty dst_buffer,
        const void *data,
        size_t size_in_byte,
        size_t dst_offset = 0);

    /// Low-level: copy into a texture (regions already prepared).
    void upload_to_texture(
        handle_ty dst_texture,
        const void *data,
        size_t size_in_byte,
        const BufferTextureCopy *regions,
        uint32_t region_count);

    /// Upload a contiguous index range into an allocated IndexBuffer.
    void upload_index_buffer_range(
        IndexBuffer *index_buffer,
        const void *data,
        uint32_t index_offset,
        uint32_t index_count);

    /// Upload a contiguous vertex attribute range into an allocated stream.
    void upload_vertex_attribute_range(
        VertexBuffer *vertex_buffer,
        VertexAttributeType::Enum type,
        const void *data,
        uint32_t vertex_offset,
        uint32_t vertex_count);

    /// CPU mip generation (when needed) + staging texture upload.
    void upload_texture_cpu_pixels(
        Texture *texture,
        const void *data,
        size_t base_level_bytes);

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    void ensure_capacity(size_t required_bytes);
    void destroy_staging() noexcept;
    void write_staging(const void *data, size_t size_in_byte);

    Device::Impl *device_ = nullptr;
    handle_ty staging_ = 0;
    size_t capacity_ = 0;
};

}// namespace ocarina
