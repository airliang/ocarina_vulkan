#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "resources/resource.h"
#include "resources/buffer.h"

namespace ocarina {

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vector4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct VertexStream {
    VertexAttributeType::Enum type;
    uint32_t count = 0;
    uint32_t offset = 0;
    uint32_t stride = 0;
    void* data = nullptr;
    Buffer* buffer = nullptr;

    uint32_t get_size() const {
        return count * stride;
    }
};

class OC_RHI_API VertexBuffer : public RHIResource {
public:
    explicit VertexBuffer(Device::Impl* device);
    ~VertexBuffer() override;

    static VertexBuffer* create_vertex_buffer(Device::Impl* device);

    void add_vertex_stream(VertexAttributeType::Enum type, uint32_t count, uint32_t stride, const void* data);

    OC_MAKE_MEMBER_GETTER(vertex_count, );

    Vector3* get_positions() {
        return static_cast<Vector3*>(vertex_streams_[(uint8_t)VertexAttributeType::Enum::Position].data);
    }

    Vector2* get_uvs() {
        return static_cast<Vector2*>(vertex_streams_[(uint8_t)VertexAttributeType::Enum::TexCoord0].data);
    }

    Vector3* get_normals() {
        return static_cast<Vector3*>(vertex_streams_[(uint8_t)VertexAttributeType::Enum::Normal].data);
    }

    VertexStream* get_vertex_stream(VertexAttributeType::Enum attribute_type) {
        if (attribute_type >= VertexAttributeType::Enum::Count) {
            return nullptr;
        }
        return &vertex_streams_[(uint8_t)attribute_type];
    }

    bool is_dirty() const {
        return dirty_;
    }

    /// Pre-allocate a GPU attribute stream with capacity (no CPU upload).
    /// GPU copies go through StagingUploader on GPUResourceThread.
    void allocate_stream_capacity(VertexAttributeType::Enum type, uint32_t capacity, uint32_t stride);

protected:
    void release_stream_buffer(VertexStream& stream);

    uint32_t vertex_count_ = 0;
    VertexStream vertex_streams_[(uint8_t)VertexAttributeType::Enum::Count];
    bool dirty_ = false;
};

}// namespace ocarina
