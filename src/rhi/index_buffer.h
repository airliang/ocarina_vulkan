#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "resources/resource.h"

namespace ocarina {

class OC_RHI_API IndexBuffer : public RHIResource {
public:
    IndexBuffer() = default;
    explicit IndexBuffer(Device::Impl* device)
        : RHIResource(device, Tag::BUFFER, 0) {}
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

    /// Upload index data to GPU (creates/replaces the device buffer).
    virtual void upload_indices(const void* data, uint32_t indices_count) = 0;

    /// Pre-allocate a fixed-capacity GPU index buffer (no CPU upload).
    virtual void allocate_capacity(uint32_t max_indices) = 0;
    /// Upload a contiguous index range into an allocated buffer.
    virtual void upload_indices_range(const void* data, uint32_t index_offset, uint32_t index_count) = 0;

protected:
    std::vector<uint16_t> indices_;
    bool bit16_ = true;
};

}// namespace ocarina
