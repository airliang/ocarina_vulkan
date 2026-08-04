#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "mesh_geometry.h"
#include "rhi/vertex_buffer.h"

namespace ocarina {

class Device;
class VertexBuffer;
class IndexBuffer;

constexpr uint64_t kMeshVertexPageBytes = 32ull * 1024ull * 1024ull;
constexpr uint32_t kMeshBytesPerVertex =
    static_cast<uint32_t>(sizeof(Vector3) + sizeof(Vector3) + sizeof(Vector2) + sizeof(Vector4));

struct MeshGeometryInput {
    uint32_t vertex_count = 0;
    const Vector3* positions = nullptr;
    const Vector3* normals = nullptr;
    const Vector2* uvs = nullptr;
    const Vector4* colors = nullptr;
    const uint16_t* indices = nullptr;
    uint32_t index_count = 0;
};

struct VertexPage {
    VertexBuffer* buffer = nullptr;
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;

    [[nodiscard]] uint64_t remaining_bytes() const noexcept {
        return total_bytes > used_bytes ? total_bytes - used_bytes : 0;
    }
};

struct IndexPage {
    IndexBuffer* buffer = nullptr;
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;

    [[nodiscard]] uint64_t remaining_bytes() const noexcept {
        return total_bytes > used_bytes ? total_bytes - used_bytes : 0;
    }
};

class VertexAllocator : public concepts::Noncopyable {
public:
    void initialize(Device* device);
    void cleanup();

    /// First-fit across pages; creates a new page when none fit.
    [[nodiscard]] uint32_t allocate(uint32_t vertex_count, uint32_t& out_vertex_offset);
    [[nodiscard]] VertexBuffer* buffer(uint32_t page_index) const;
    [[nodiscard]] size_t page_count() const noexcept { return pages_.size(); }

private:
    uint32_t create_page(uint64_t total_bytes);
    void allocate_page_streams(VertexPage& page, uint32_t vertex_capacity);

    Device* device_ = nullptr;
    std::vector<VertexPage> pages_;
};

class IndexAllocator : public concepts::Noncopyable {
public:
    void initialize(Device* device);
    void cleanup();

    [[nodiscard]] uint32_t allocate(uint32_t index_count, uint32_t& out_index_offset);
    [[nodiscard]] IndexBuffer* buffer(uint32_t page_index) const;
    [[nodiscard]] size_t page_count() const noexcept { return pages_.size(); }

private:
    uint32_t create_page(uint64_t total_bytes);
    void allocate_page_buffer(IndexPage& page, uint32_t index_capacity);

    Device* device_ = nullptr;
    std::vector<IndexPage> pages_;
};

/// Owns paged mega VB/IB storage and performs GPU uploads into pages.
class MeshBufferAllocator : public concepts::Noncopyable {
public:
    void initialize(Device* device);
    void cleanup();

    /// Allocate page space and upload mesh geometry. Returns page-local slice.
    [[nodiscard]] MeshGeometrySlice upload(const MeshGeometryInput& input);

    [[nodiscard]] VertexBuffer* vertex_buffer(uint32_t page_index) const;
    [[nodiscard]] IndexBuffer* index_buffer(uint32_t page_index) const;

private:
    Device* device_ = nullptr;
    VertexAllocator vertex_allocator_;
    IndexAllocator index_allocator_;
};

}// namespace ocarina
