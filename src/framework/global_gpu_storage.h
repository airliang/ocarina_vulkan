#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "mesh_buffer_allocator.h"

namespace ocarina {

class Device;
class Mesh;
class VertexBuffer;
class IndexBuffer;

/// Owned mesh attribute arrays (moved into GPUResourceRequest).
struct OwnedMeshGeometry {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector4> colors;
    std::vector<uint16_t> indices;
};

/// Facade over MeshBufferAllocator for mesh GPU uploads.
class GlobalGPUStorage : public concepts::Noncopyable {
public:
    static GlobalGPUStorage& instance();

    void initialize(Device* device);
    void cleanup();

    /// Enqueue mesh upload onto the GPU resource thread.
    void upload_mesh(OwnedMeshGeometry&& geometry, Mesh* mesh);

    /// Allocate + upload immediately (GPU resource thread only, or when thread is not running).
    [[nodiscard]] MeshGeometrySlice upload_geometry(const MeshGeometryInput& input);

    [[nodiscard]] VertexBuffer* vertex_buffer(uint32_t page_index) const;
    [[nodiscard]] IndexBuffer* index_buffer(uint32_t page_index) const;

private:
    GlobalGPUStorage() = default;
    ~GlobalGPUStorage();

    Device* device_ = nullptr;
    MeshBufferAllocator allocator_;
};

}// namespace ocarina
