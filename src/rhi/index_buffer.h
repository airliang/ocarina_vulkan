#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "resources/resource.h"
#include "resources/buffer.h"

namespace ocarina {

class OC_RHI_API IndexBuffer : public RHIResource {
public:
    IndexBuffer() = default;
    explicit IndexBuffer(Device::Impl* device);
    IndexBuffer(Device::Impl* device, const void* initial_data, uint32_t indices_count, bool bit16);
    ~IndexBuffer() override;

    static IndexBuffer* create_index_buffer(
        Device::Impl* device,
        void* initial_data,
        uint32_t indices_count,
        bool bit16 = true);

    void set_indices(std::vector<uint16_t>&& indices) {
        indices_ = std::move(indices);
    }

    uint32_t get_index_count() const {
        return static_cast<uint32_t>(indices_.size());
    }

    bool is_16_bit() const {
        return bit16_;
    }

    [[nodiscard]] Buffer* buffer() const noexcept { return buffer_; }
    [[nodiscard]] handle_ty buffer_handle() const noexcept {
        return reinterpret_cast<handle_ty>(buffer_);
    }

    /// Upload index data to GPU (creates/replaces the device buffer).
    void upload_indices(const void* data, uint32_t indices_count);

    /// Pre-allocate a fixed-capacity GPU index buffer (no CPU upload).
    void allocate_capacity(uint32_t max_indices);
    /// Upload a contiguous index range into an allocated buffer.
    void upload_indices_range(const void* data, uint32_t index_offset, uint32_t index_count);

protected:
    void release_buffer();
    void load_from_cpu(const void* cpu_data, uint32_t num_bytes);

    std::vector<uint16_t> indices_;
    bool bit16_ = true;
    Buffer* buffer_ = nullptr;
    uint32_t capacity_indices_ = 0;
};

}// namespace ocarina
