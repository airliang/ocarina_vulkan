#include "vertex_buffer.h"
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
        "vertex_staging");
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

VertexBuffer::VertexBuffer(Device::Impl* device)
    : RHIResource(device, Tag::BUFFER, 0) {}

VertexBuffer::~VertexBuffer() {
    for (size_t i = 0; i < (size_t)VertexAttributeType::Enum::Count; ++i) {
        VertexStream& stream = vertex_streams_[i];
        if (stream.data) {
            delete[] static_cast<uint8_t*>(stream.data);
            stream.data = nullptr;
        }
        release_stream_buffer(stream);
    }
}

VertexBuffer* VertexBuffer::create_vertex_buffer(Device::Impl* device) {
    return device->create_vertex_buffer();
}

void VertexBuffer::release_stream_buffer(VertexStream& stream) {
    if (stream.buffer == nullptr || device_ == nullptr) {
        stream.buffer = nullptr;
        return;
    }
    device_->destroy_buffer(reinterpret_cast<handle_ty>(stream.buffer));
    stream.buffer = nullptr;
}

void VertexBuffer::add_vertex_stream(
    VertexAttributeType::Enum type,
    uint32_t count,
    uint32_t stride,
    const void* data) {
    if (vertex_streams_[(uint8_t)type].data) {
        delete[] static_cast<uint8_t*>(vertex_streams_[(uint8_t)type].data);
        vertex_streams_[(uint8_t)type].data = nullptr;
    }

    if (data != nullptr) {
        vertex_streams_[(uint8_t)type].data = new uint8_t[count * stride];
        memcpy(vertex_streams_[(uint8_t)type].data, data, count * stride);
    }

    vertex_streams_[(uint8_t)type].type = type;
    vertex_streams_[(uint8_t)type].count = count;
    vertex_streams_[(uint8_t)type].stride = stride;
    vertex_streams_[(uint8_t)type].offset = 0;
    if (type == VertexAttributeType::Enum::Position) {
        vertex_count_ = count;
    }
    dirty_ = true;
}

void VertexBuffer::upload_data() {
    for (auto& stream : vertex_streams_) {
        if (stream.data) {
            upload_attribute_data(stream.type, stream.data, stream.offset);
        }
    }
    dirty_ = false;
    set_gpu_resource_state(GPUResourceState::GPU_Ready);
}

void VertexBuffer::allocate_stream_capacity(
    VertexAttributeType::Enum type,
    uint32_t capacity,
    uint32_t stride) {
    if (capacity == 0 || stride == 0 || device_ == nullptr) {
        return;
    }

    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr) {
        return;
    }

    stream->type = type;
    stream->count = capacity;
    stream->stride = stride;
    stream->offset = 0;
    if (type == VertexAttributeType::Enum::Position) {
        vertex_count_ = capacity;
    }

    const uint64_t byte_size = static_cast<uint64_t>(capacity) * stride;
    release_stream_buffer(*stream);
    stream->buffer = reinterpret_cast<Buffer*>(device_->create_gpu_buffer(
        byte_size,
        GraphicBufferBindFlags::VertexBuffer));
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

void VertexBuffer::upload_attribute_range(
    VertexAttributeType::Enum type,
    const void* data,
    uint32_t vertex_offset,
    uint32_t vertex_count) {
    if (data == nullptr || vertex_count == 0 || device_ == nullptr) {
        return;
    }

    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr || stream->buffer == nullptr || stream->stride == 0) {
        return;
    }
    OC_ASSERT(vertex_offset + vertex_count <= stream->count);

    const uint64_t byte_size = static_cast<uint64_t>(vertex_count) * stream->stride;
    const uint64_t dst_offset = static_cast<uint64_t>(vertex_offset) * stream->stride;
    upload_buffer_via_staging(device_, stream->buffer, data, byte_size, dst_offset);
}

void VertexBuffer::upload_attribute_data(
    VertexAttributeType::Enum type,
    const void* data,
    uint64_t offset) {
    (void)data;
    if (device_ == nullptr) {
        return;
    }

    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr || stream->data == nullptr) {
        return;
    }

    if (stream->buffer == nullptr) {
        stream->buffer = reinterpret_cast<Buffer*>(device_->create_gpu_buffer(
            stream->get_size(),
            GraphicBufferBindFlags::VertexBuffer));
    }

    upload_buffer_via_staging(
        device_,
        stream->buffer,
        stream->data,
        stream->get_size(),
        offset);
}

}// namespace ocarina
