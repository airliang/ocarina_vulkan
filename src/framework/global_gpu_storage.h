#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "mesh_buffer_allocator.h"
#include "mesh_cpu_memory.h"

namespace ocarina {

class Device;
class Mesh;
class VertexBuffer;
class IndexBuffer;

/// Owned mesh attribute arrays (moved into GPUResourceRequest).
struct OwnedMeshGeometry {
    MeshPositions positions = make_mesh_positions();
    MeshNormals normals = make_mesh_normals();
    MeshTangents tangents = make_mesh_tangents();
    MeshUvs uvs = make_mesh_uvs();
    MeshColors colors = make_mesh_colors();
    MeshIndices indices = make_mesh_indices();
};

/// Facade over MeshBufferAllocator for mesh GPU uploads.
class GlobalGPUStorage : public concepts::Noncopyable {
public:
    static GlobalGPUStorage& instance();

    void initialize(Device* device);
    void cleanup();

    [[nodiscard]] MeshGeometrySlice upload_geometry(const MeshGeometryInput& input);
    void upload_mesh(OwnedMeshGeometry&& geometry, Mesh* mesh);

    [[nodiscard]] VertexBuffer* vertex_buffer(uint32_t page_index) const;
    [[nodiscard]] IndexBuffer* index_buffer(uint32_t page_index) const;

    [[nodiscard]] Device* device() const noexcept { return device_; }
    [[nodiscard]] MeshBufferAllocator& allocator() noexcept { return allocator_; }

private:
    GlobalGPUStorage() = default;
    ~GlobalGPUStorage();

    Device* device_ = nullptr;
    MeshBufferAllocator allocator_;
};

}// namespace ocarina
