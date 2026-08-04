#include "mesh_buffer_allocator.h"
#include "rhi/device.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/resource.h"
#include "core/logging.h"
#include <algorithm>

namespace ocarina {

namespace {

constexpr Vector3 kDefaultNormal = {0.0f, 0.0f, 1.0f};
constexpr Vector2 kDefaultUv = {0.0f, 0.0f};
constexpr Vector4 kDefaultColor = {1.0f, 1.0f, 1.0f, 1.0f};

[[nodiscard]] uint32_t vertex_capacity_for_bytes(uint64_t total_bytes) noexcept {
    return static_cast<uint32_t>(total_bytes / kMeshBytesPerVertex);
}

[[nodiscard]] uint64_t default_index_page_bytes() noexcept {
    const uint32_t vertex_capacity = vertex_capacity_for_bytes(kMeshVertexPageBytes);
    // Derive IB page size from VB page vertex capacity (assume ~3 indices per vertex).
    return static_cast<uint64_t>(vertex_capacity) * 3ull * sizeof(uint16_t);
}

[[nodiscard]] uint32_t index_capacity_for_bytes(uint64_t total_bytes) noexcept {
    return static_cast<uint32_t>(total_bytes / sizeof(uint16_t));
}

}// namespace

void VertexAllocator::initialize(Device* device) {
    device_ = device;
}

void VertexAllocator::cleanup() {
    for (VertexPage& page : pages_) {
        if (page.buffer != nullptr) {
            ocarina::delete_with_allocator<VertexBuffer>(page.buffer);
            page.buffer = nullptr;
        }
    }
    pages_.clear();
    device_ = nullptr;
}

void VertexAllocator::allocate_page_streams(VertexPage& page, uint32_t vertex_capacity) {
    OC_ASSERT(page.buffer != nullptr);
    page.buffer->allocate_stream_capacity(
        VertexAttributeType::Enum::Position, vertex_capacity, sizeof(Vector3));
    page.buffer->allocate_stream_capacity(
        VertexAttributeType::Enum::Normal, vertex_capacity, sizeof(Vector3));
    page.buffer->allocate_stream_capacity(
        VertexAttributeType::Enum::TexCoord0, vertex_capacity, sizeof(Vector2));
    page.buffer->allocate_stream_capacity(
        VertexAttributeType::Enum::Color0, vertex_capacity, sizeof(Vector4));
}

uint32_t VertexAllocator::create_page(uint64_t total_bytes) {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(total_bytes >= kMeshBytesPerVertex);

    VertexPage page;
    page.total_bytes = total_bytes;
    page.used_bytes = 0;
    page.buffer = device_->create_vertex_buffer();
    allocate_page_streams(page, vertex_capacity_for_bytes(total_bytes));

    const uint32_t page_index = static_cast<uint32_t>(pages_.size());
    pages_.push_back(page);
    return page_index;
}

uint32_t VertexAllocator::allocate(uint32_t vertex_count, uint32_t& out_vertex_offset) {
    OC_ASSERT(vertex_count > 0);
    const uint64_t needed_bytes = static_cast<uint64_t>(vertex_count) * kMeshBytesPerVertex;

    for (uint32_t page_index = 0; page_index < pages_.size(); ++page_index) {
        VertexPage& page = pages_[page_index];
        if (page.remaining_bytes() >= needed_bytes) {
            out_vertex_offset = static_cast<uint32_t>(page.used_bytes / kMeshBytesPerVertex);
            page.used_bytes += needed_bytes;
            return page_index;
        }
    }

    const uint64_t page_bytes = std::max(kMeshVertexPageBytes, needed_bytes);
    const uint32_t page_index = create_page(page_bytes);
    VertexPage& page = pages_[page_index];
    out_vertex_offset = 0;
    page.used_bytes = needed_bytes;
    return page_index;
}

VertexBuffer* VertexAllocator::buffer(uint32_t page_index) const {
    if (page_index >= pages_.size()) {
        return nullptr;
    }
    return pages_[page_index].buffer;
}

void IndexAllocator::initialize(Device* device) {
    device_ = device;
}

void IndexAllocator::cleanup() {
    for (IndexPage& page : pages_) {
        if (page.buffer != nullptr) {
            ocarina::delete_with_allocator<IndexBuffer>(page.buffer);
            page.buffer = nullptr;
        }
    }
    pages_.clear();
    device_ = nullptr;
}

void IndexAllocator::allocate_page_buffer(IndexPage& page, uint32_t index_capacity) {
    OC_ASSERT(page.buffer != nullptr);
    page.buffer->allocate_capacity(index_capacity);
}

uint32_t IndexAllocator::create_page(uint64_t total_bytes) {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(total_bytes >= sizeof(uint16_t));

    IndexPage page;
    page.total_bytes = total_bytes;
    page.used_bytes = 0;
    page.buffer = device_->create_index_buffer(nullptr, 0, true);
    allocate_page_buffer(page, index_capacity_for_bytes(total_bytes));

    const uint32_t page_index = static_cast<uint32_t>(pages_.size());
    pages_.push_back(page);
    return page_index;
}

uint32_t IndexAllocator::allocate(uint32_t index_count, uint32_t& out_index_offset) {
    OC_ASSERT(index_count > 0);
    const uint64_t needed_bytes = static_cast<uint64_t>(index_count) * sizeof(uint16_t);

    for (uint32_t page_index = 0; page_index < pages_.size(); ++page_index) {
        IndexPage& page = pages_[page_index];
        if (page.remaining_bytes() >= needed_bytes) {
            out_index_offset = static_cast<uint32_t>(page.used_bytes / sizeof(uint16_t));
            page.used_bytes += needed_bytes;
            return page_index;
        }
    }

    const uint64_t page_bytes = std::max(default_index_page_bytes(), needed_bytes);
    const uint32_t page_index = create_page(page_bytes);
    IndexPage& page = pages_[page_index];
    out_index_offset = 0;
    page.used_bytes = needed_bytes;
    return page_index;
}

IndexBuffer* IndexAllocator::buffer(uint32_t page_index) const {
    if (page_index >= pages_.size()) {
        return nullptr;
    }
    return pages_[page_index].buffer;
}

void MeshBufferAllocator::initialize(Device* device) {
    device_ = device;
    vertex_allocator_.initialize(device);
    index_allocator_.initialize(device);
}

void MeshBufferAllocator::cleanup() {
    vertex_allocator_.cleanup();
    index_allocator_.cleanup();
    device_ = nullptr;
}

MeshGeometrySlice MeshBufferAllocator::upload(const MeshGeometryInput& input) {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(input.vertex_count > 0);
    OC_ASSERT(input.positions != nullptr);
    OC_ASSERT(input.indices != nullptr);
    OC_ASSERT(input.index_count > 0);

    MeshGeometrySlice slice;
    slice.vertex_count = input.vertex_count;
    slice.index_count = input.index_count;
    slice.vertex_page = vertex_allocator_.allocate(input.vertex_count, slice.vertex_offset);
    slice.index_page = index_allocator_.allocate(input.index_count, slice.index_offset);

    VertexBuffer* vertex_buffer = vertex_allocator_.buffer(slice.vertex_page);
    IndexBuffer* index_buffer = index_allocator_.buffer(slice.index_page);
    OC_ASSERT(vertex_buffer != nullptr);
    OC_ASSERT(index_buffer != nullptr);

    vertex_buffer->upload_attribute_range(
        VertexAttributeType::Enum::Position,
        input.positions,
        slice.vertex_offset,
        input.vertex_count);

    if (input.normals != nullptr) {
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::Normal,
            input.normals,
            slice.vertex_offset,
            input.vertex_count);
    } else {
        std::vector<Vector3> normals(input.vertex_count, kDefaultNormal);
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::Normal,
            normals.data(),
            slice.vertex_offset,
            input.vertex_count);
    }

    if (input.uvs != nullptr) {
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::TexCoord0,
            input.uvs,
            slice.vertex_offset,
            input.vertex_count);
    } else {
        std::vector<Vector2> uvs(input.vertex_count, kDefaultUv);
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::TexCoord0,
            uvs.data(),
            slice.vertex_offset,
            input.vertex_count);
    }

    if (input.colors != nullptr) {
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::Color0,
            input.colors,
            slice.vertex_offset,
            input.vertex_count);
    } else {
        std::vector<Vector4> colors(input.vertex_count, kDefaultColor);
        vertex_buffer->upload_attribute_range(
            VertexAttributeType::Enum::Color0,
            colors.data(),
            slice.vertex_offset,
            input.vertex_count);
    }

    index_buffer->upload_indices_range(
        input.indices,
        slice.index_offset,
        input.index_count);

    return slice;
}

VertexBuffer* MeshBufferAllocator::vertex_buffer(uint32_t page_index) const {
    return vertex_allocator_.buffer(page_index);
}

IndexBuffer* MeshBufferAllocator::index_buffer(uint32_t page_index) const {
    return index_allocator_.buffer(page_index);
}

}// namespace ocarina
