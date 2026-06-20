#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "mesh_geometry.h"
#include "rhi/vertex_buffer.h"

namespace ocarina {

class Device;
class VertexBuffer;
class IndexBuffer;

struct MeshGeometryInput {
    uint32_t vertex_count = 0;
    const Vector3* positions = nullptr;
    const Vector3* normals = nullptr;
    const Vector2* uvs = nullptr;
    const Vector4* colors = nullptr;
    const uint16_t* indices = nullptr;
    uint32_t index_count = 0;
};

/// Mega vertex/index buffers (SoA: position, normal, uv, color).
class GlobalGPUStorage : public concepts::Noncopyable {
public:
    static GlobalGPUStorage& instance();

    void initialize(Device* device);
    void cleanup();
    [[nodiscard]] bool is_finalized() const { return uploaded_; }

    MeshGeometrySlice append_mesh(const MeshGeometryInput& input);
    void finalize();

    [[nodiscard]] VertexBuffer* vertex_buffer() const { return vertex_buffer_; }
    [[nodiscard]] IndexBuffer* index_buffer() const { return index_buffer_; }

private:
    GlobalGPUStorage() = default;
    ~GlobalGPUStorage();
    void upload_mega_buffers();
    void release_staging_buffers();

    Device* device_ = nullptr;
    VertexBuffer* vertex_buffer_ = nullptr;
    IndexBuffer* index_buffer_ = nullptr;

    std::vector<Vector3> positions_;
    std::vector<Vector3> normals_;
    std::vector<Vector2> uvs_;
    std::vector<Vector4> colors_;
    std::vector<uint16_t> indices_;

    bool uploaded_ = false;
};

}// namespace ocarina
