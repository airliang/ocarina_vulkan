#include "vertex_buffer.h"
#include "device.h"

namespace ocarina {

VertexBuffer::~VertexBuffer() {}

VertexBuffer* VertexBuffer::create_vertex_buffer(Device::Impl* device) {
    return device->create_vertex_buffer();
}

void VertexBuffer::add_vertex_stream(
    VertexAttributeType::Enum type,
    uint32_t count,
    uint32_t stride,
    const void* data) {
    if (vertex_streams_[(uint8_t)type].data) {
        delete[] vertex_streams_[(uint8_t)type].data;
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

}// namespace ocarina
