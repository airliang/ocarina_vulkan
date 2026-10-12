#include "vertex_buffer.h"
#include "device.h"

namespace ocarina {

VertexBuffer::VertexBuffer(Device::Impl *device)
    : RHIResource(device, Tag::BUFFER, 0) {}

VertexBuffer::~VertexBuffer() {
    for (size_t i = 0; i < (size_t)VertexAttributeType::Enum::Count; ++i) {
        release_stream_buffer(vertex_streams_[i]);
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
