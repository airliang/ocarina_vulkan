#include "index_buffer.h"
#include "device.h"
#include "command_buffer.h"
#include "fence.h"
#include "core/logging.h"

namespace ocarina {

namespace {

void upload_buffer_via_staging(
    Device::Impl* device,
    Buffer* dst,
    const void* data,
    size_t size_in_byte,
    size_t dst_offset) {
    if (device == nullptr || dst == nullptr || data == nullptr || size_in_byte == 0) {
        return;
    }

    const handle_ty staging_handle = device->create_buffer(
        size_in_byte,
        GraphicBufferBindFlags::CopySrc,
        "index_staging");
    Buffer* staging = reinterpret_cast<Buffer*>(staging_handle);
    if (staging == nullptr) {
        return;
    }

    staging->copy_from_immediately(data, static_cast<uint32_t>(size_in_byte));

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    cmd.copy_buffer(
        staging_handle,
        reinterpret_cast<handle_ty>(dst),
        0,
        dst_offset,
        size_in_byte);
    cmd.end();

    Fence fence = device->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
    device->destroy_buffer(staging_handle);
}

}// namespace

IndexBuffer::IndexBuffer(Device::Impl* device)
    : RHIResource(device, Tag::BUFFER, 0) {}

IndexBuffer::IndexBuffer(
    Device::Impl* device,
    const void* initial_data,
    uint32_t indices_count,
    bool bit16)
    : RHIResource(device, Tag::BUFFER, 0) {
    bit16_ = bit16;
    if (indices_count > 0 && initial_data != nullptr) {
        upload_indices(initial_data, indices_count);
    }
}

IndexBuffer::~IndexBuffer() {
    release_buffer();
}

IndexBuffer* IndexBuffer::create_index_buffer(
    Device::Impl* device,
    void* initial_data,
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
    buffer_ = reinterpret_cast<Buffer*>(device_->create_gpu_buffer(
        num_bytes,
        GraphicBufferBindFlags::IndexBuffer));
    capacity_indices_ = max_indices;
    indices_.clear();
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

void IndexBuffer::upload_indices_range(
    const void* data,
    uint32_t index_offset,
    uint32_t index_count) {
    if (data == nullptr || index_count == 0 || device_ == nullptr) {
        return;
    }
    OC_ASSERT(buffer_ != nullptr);
    OC_ASSERT(index_offset + index_count <= capacity_indices_);

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint64_t num_bytes = static_cast<uint64_t>(index_count) * stride;
    const uint64_t dst_offset = static_cast<uint64_t>(index_offset) * stride;
    upload_buffer_via_staging(device_, buffer_, data, num_bytes, dst_offset);
}

void IndexBuffer::upload_indices(const void* data, uint32_t indices_count) {
    if (data == nullptr || indices_count == 0) {
        return;
    }

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint32_t num_bytes = indices_count * stride;
    indices_.resize(indices_count);
    memcpy(indices_.data(), data, num_bytes);
    load_from_cpu(data, num_bytes);
    set_gpu_resource_state(GPUResourceState::GPU_Ready);
}

void IndexBuffer::load_from_cpu(const void* cpu_data, uint32_t num_bytes) {
    if (num_bytes == 0 || cpu_data == nullptr || device_ == nullptr) {
        return;
    }

    release_buffer();
    buffer_ = reinterpret_cast<Buffer*>(device_->create_gpu_buffer(
        num_bytes,
        GraphicBufferBindFlags::IndexBuffer));
    upload_buffer_via_staging(device_, buffer_, cpu_data, num_bytes, 0);

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    capacity_indices_ = num_bytes / stride;
}

}// namespace ocarina
