#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "device.h"

namespace ocarina {

class OC_RHI_API IndexBuffer {
public:
    IndexBuffer() = default;
    virtual ~IndexBuffer();

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

protected:
    std::vector<uint16_t> indices_;
    Device::Impl* device_ = nullptr;
    bool bit16_ = true;
};

}// namespace ocarina
