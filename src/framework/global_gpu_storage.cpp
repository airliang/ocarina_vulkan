#include "global_gpu_storage.h"

#include "rhi/device.h"
#include "rhi/index_buffer.h"

namespace ocarina {

GlobalGPUStorage& GlobalGPUStorage::instance() {
    static GlobalGPUStorage s_instance;
    return s_instance;
}

GlobalGPUStorage::~GlobalGPUStorage() {
    cleanup();
}

void GlobalGPUStorage::initialize(Device* device) {
    if (device_ != nullptr && device_ != device) {
        cleanup();
    }
    device_ = device;
}

void GlobalGPUStorage::cleanup() {
    if (vertex_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VertexBuffer>(vertex_buffer_);
        vertex_buffer_ = nullptr;
    }
    if (index_buffer_ != nullptr) {
        ocarina::delete_with_allocator<IndexBuffer>(index_buffer_);
        index_buffer_ = nullptr;
    }

    release_staging_buffers();

    uploaded_ = false;
}

MeshGeometrySlice GlobalGPUStorage::append_mesh(const MeshGeometryInput& input) {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(!uploaded_);
    OC_ASSERT(input.vertex_count > 0);
    OC_ASSERT(input.positions != nullptr);
    OC_ASSERT(input.indices != nullptr);
    OC_ASSERT(input.index_count > 0);

    MeshGeometrySlice slice;
    slice.vertex_offset = static_cast<uint32_t>(positions_.size());
    slice.vertex_count = input.vertex_count;
    slice.index_offset = static_cast<uint32_t>(indices_.size());
    slice.index_count = input.index_count;

    constexpr Vector3 kDefaultNormal = {0.0f, 0.0f, 1.0f};
    constexpr Vector2 kDefaultUv = {0.0f, 0.0f};
    constexpr Vector4 kDefaultColor = {1.0f, 1.0f, 1.0f, 1.0f};

    positions_.reserve(positions_.size() + input.vertex_count);
    normals_.reserve(normals_.size() + input.vertex_count);
    uvs_.reserve(uvs_.size() + input.vertex_count);
    colors_.reserve(colors_.size() + input.vertex_count);
    indices_.reserve(indices_.size() + input.index_count);

    if (input.normals != nullptr && normals_.size() < slice.vertex_offset) {
        normals_.insert(normals_.end(), slice.vertex_offset - normals_.size(), kDefaultNormal);
    }
    if (input.uvs != nullptr && uvs_.size() < slice.vertex_offset) {
        uvs_.insert(uvs_.end(), slice.vertex_offset - uvs_.size(), kDefaultUv);
    }
    if (input.colors != nullptr && colors_.size() < slice.vertex_offset) {
        colors_.insert(colors_.end(), slice.vertex_offset - colors_.size(), kDefaultColor);
    }

    for (uint32_t vertex_index = 0; vertex_index < input.vertex_count; ++vertex_index) {
        positions_.push_back(input.positions[vertex_index]);
        if (input.normals != nullptr) {
            normals_.push_back(input.normals[vertex_index]);
        }
        if (input.uvs != nullptr) {
            uvs_.push_back(input.uvs[vertex_index]);
        }
        if (input.colors != nullptr) {
            colors_.push_back(input.colors[vertex_index]);
        }
    }

    for (uint32_t index = 0; index < input.index_count; ++index) {
        // Keep mesh-local indices; vertex_offset is applied at draw time via draw_indexed.
        indices_.push_back(static_cast<uint16_t>(input.indices[index]));
    }

    return slice;
}

void GlobalGPUStorage::upload_mega_buffers() {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(!positions_.empty());
    OC_ASSERT(!indices_.empty());

    constexpr Vector3 kDefaultNormal = {0.0f, 0.0f, 1.0f};
    constexpr Vector2 kDefaultUv = {0.0f, 0.0f};
    constexpr Vector4 kDefaultColor = {1.0f, 1.0f, 1.0f, 1.0f};

    const size_t vertex_count = positions_.size();
    if (normals_.size() != vertex_count) {
        normals_.resize(vertex_count, kDefaultNormal);
    }
    if (uvs_.size() != vertex_count) {
        uvs_.resize(vertex_count, kDefaultUv);
    }
    if (colors_.size() != vertex_count) {
        colors_.resize(vertex_count, kDefaultColor);
    }

    OC_ASSERT(normals_.size() == vertex_count);
    OC_ASSERT(uvs_.size() == vertex_count);
    OC_ASSERT(colors_.size() == vertex_count);

    vertex_buffer_ = device_->create_vertex_buffer();
    const uint32_t vertex_count_u32 = static_cast<uint32_t>(vertex_count);
    vertex_buffer_->add_vertex_stream(
        VertexAttributeType::Enum::Position,
        vertex_count_u32,
        sizeof(Vector3),
        positions_.data());
    vertex_buffer_->add_vertex_stream(
        VertexAttributeType::Enum::Normal,
        vertex_count_u32,
        sizeof(Vector3),
        normals_.data());
    vertex_buffer_->add_vertex_stream(
        VertexAttributeType::Enum::TexCoord0,
        vertex_count_u32,
        sizeof(Vector2),
        uvs_.data());
    vertex_buffer_->add_vertex_stream(
        VertexAttributeType::Enum::Color0,
        vertex_count_u32,
        sizeof(Vector4),
        colors_.data());
    vertex_buffer_->upload_data();

    index_buffer_ = device_->create_index_buffer(
        indices_.data(),
        static_cast<uint32_t>(indices_.size()),
        true);
}

void GlobalGPUStorage::release_staging_buffers() {
    positions_ = {};
    normals_ = {};
    uvs_ = {};
    colors_ = {};
    indices_ = {};
}

void GlobalGPUStorage::finalize() {
    if (uploaded_ || device_ == nullptr || positions_.empty()) {
        return;
    }
    upload_mega_buffers();
    release_staging_buffers();
    uploaded_ = true;
}

}// namespace ocarina
