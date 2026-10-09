#include "vertex_buffer.h"
#include "device.h"

namespace ocarina {

VertexBuffer::VertexBuffer(Device::Impl *device)
    : RHIResource(device, Tag::BUFFER, 0) {}

VertexBuffer::~VertexBuffer() {
    for (size_t i = 0; i < (size_t)VertexAttributeType::Enum::Count; ++i) {
        VertexStream &stream = vertex_streams_[i];
        if (stream.data) {
            delete[] static_cast<uint8_t *>(stream.data);
            stream.data = nullptr;
        }
        release_stream_buffer(stream);
    }
}

VertexBuffer *VertexBuffer::create_vertex_buffer(Device::Impl *device) {
    return device->create_vertex_buffer();
}

void VertexBuffer::release_stream_buffer(VertexStream &stream) {
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
    const void *data) {
    if (vertex_streams_[(uint8_t)type].data) {
        delete[] static_cast<uint8_t *>(vertex_streams_[(uint8_t)type].data);
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

void VertexBuffer::allocate_stream_capacity(
    VertexAttributeType::Enum type,
    uint32_t capacity,
    uint32_t stride) {
    if (capacity == 0 || stride == 0 || device_ == nullptr) {
        return;
    }

    VertexStream *stream = get_vertex_stream(type);
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
    stream->buffer = reinterpret_cast<Buffer *>(device_->create_gpu_buffer(
        byte_size,
        GraphicBufferBindFlags::VertexBuffer));
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

}// namespace ocarina
