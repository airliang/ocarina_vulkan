#include "index_buffer.h"
#include "device.h"

namespace ocarina {

IndexBuffer::IndexBuffer(Device::Impl *device)
    : RHIResource(device, Tag::BUFFER, 0) {}

IndexBuffer::IndexBuffer(
    Device::Impl *device,
    const void *initial_data,
    uint32_t indices_count,
    bool bit16)
    : RHIResource(device, Tag::BUFFER, 0) {
    bit16_ = bit16;
    if (indices_count == 0) {
        return;
    }
    allocate_capacity(indices_count);
    if (initial_data != nullptr) {
        const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
        indices_.resize(indices_count);
        memcpy(indices_.data(), initial_data, static_cast<size_t>(indices_count) * stride);
    }
}

IndexBuffer::~IndexBuffer() {
    release_buffer();
}

IndexBuffer *IndexBuffer::create_index_buffer(
    Device::Impl *device,
    void *initial_data,
    uint32_t indices_count,
    bool bit16) {
    return device->create_index_buffer(initial_data, indices_count, bit16);
}

void IndexBuffer::release_buffer() {
    if (buffer_ == nullptr || device_ == nullptr) {
        buffer_ = nullptr;
        capacity_indices_ = 0;
        return;
    }
    device_->destroy_buffer(reinterpret_cast<handle_ty>(buffer_));
    buffer_ = nullptr;
    capacity_indices_ = 0;
}

void IndexBuffer::allocate_capacity(uint32_t max_indices) {
    if (max_indices == 0 || device_ == nullptr) {
        return;
    }

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint64_t num_bytes = static_cast<uint64_t>(max_indices) * stride;

    release_buffer();
    buffer_ = reinterpret_cast<Buffer *>(device_->create_gpu_buffer(
        num_bytes,
        GraphicBufferBindFlags::IndexBuffer));
    capacity_indices_ = max_indices;
    indices_.clear();
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

}// namespace ocarina
