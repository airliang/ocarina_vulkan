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

    void set_indices(ocarina_vector<uint16_t>&& indices) {
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

    /// Pre-allocate a fixed-capacity GPU index buffer (no CPU upload).
    /// GPU copies go through StagingUploader on GPUResourceThread.
    void allocate_capacity(uint32_t max_indices);

protected:
    void release_buffer();

    ocarina_vector<uint16_t> indices_;
    bool bit16_ = true;
    Buffer* buffer_ = nullptr;
    uint32_t capacity_indices_ = 0;
};

}// namespace ocarina
