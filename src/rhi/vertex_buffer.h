#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "resources/resource.h"
#include "resources/buffer.h"

namespace ocarina {

/// Packed GPU/CPU mesh attribute vectors.
/// Do NOT alias these to ktm::fvec*: ktm::fvec3 is alignas(16) (sizeof 16),
/// while vertex shaders and pipeline bindings use R32G32B32_SFLOAT with stride 12.
/// Using padded ktm vectors here scrambles every mesh (positions/normals/colors).
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

static_assert(sizeof(Vector2) == 8, "Vector2 must stay tightly packed for mesh VB strides");
static_assert(sizeof(Vector3) == 12, "Vector3 must stay tightly packed for mesh VB strides");
static_assert(sizeof(Vector4) == 16, "Vector4 must stay tightly packed for mesh VB strides");

struct VertexStream {
    VertexAttributeType::Enum type;
    uint32_t count = 0;
    uint32_t offset = 0;
    uint32_t stride = 0;
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

    OC_MAKE_MEMBER_GETTER(vertex_count, );

    VertexStream* get_vertex_stream(VertexAttributeType::Enum attribute_type) {
        if (attribute_type >= VertexAttributeType::Enum::Count) {
            return nullptr;
        }
        return &vertex_streams_[(uint8_t)attribute_type];
    }

    /// Pre-allocate a GPU attribute stream with capacity (no CPU upload).
    /// GPU copies go through StagingUploader on GPUResourceThread.
    void allocate_stream_capacity(VertexAttributeType::Enum type, uint32_t capacity, uint32_t stride);

protected:
    void release_stream_buffer(VertexStream& stream);

    uint32_t vertex_count_ = 0;
    VertexStream vertex_streams_[(uint8_t)VertexAttributeType::Enum::Count];
};

}// namespace ocarina
