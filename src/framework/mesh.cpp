#pragma once

#include "mesh.h"
#include "rhi/device.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "resource_manager.h"

namespace ocarina {

Mesh::~Mesh() {
    if (vertex_buffer_) {
        ocarina::delete_with_allocator<VertexBuffer>(vertex_buffer_);
    }

    if (index_buffer_) {
        ocarina::delete_with_allocator<IndexBuffer>(index_buffer_);
    }
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
}

Mesh*Mesh::create_quad(Device *device) {
    Mesh* mesh = ResourceManager::instance().get_mesh("quad");
    if (!mesh) {
        mesh = ocarina::new_with_allocator<Quad>(device);
        ResourceManager::instance().add_mesh("quad", mesh);
    }
    return mesh;
}

void BuildinMesh::create_buildin_mesh(Device* device) {
    if (vertex_buffer_[0] == nullptr) {
        vertex_buffer_[0] = device->create_vertex_buffer();
        vertex_buffer_[1] = device->create_vertex_buffer();
        vertex_buffer_[2] = device->create_vertex_buffer();
    }
    if (index_buffer_[0] == nullptr) {
        index_buffer_[0] = device->create_index_buffer(nullptr, 0);
        index_buffer_[1] = device->create_index_buffer(nullptr, 0);
        index_buffer_[2] = device->create_index_buffer(nullptr, 0);
    }

    is_created_ = true;
}

void BuildinMesh::cleanup() {

    for (int i = 0; i < 3; ++i) {
        ocarina::delete_with_allocator<VertexBuffer>(vertex_buffer_[i]);
        vertex_buffer_[i] = nullptr;
        ocarina::delete_with_allocator<IndexBuffer>(index_buffer_[i]);
        index_buffer_[i] = nullptr;
    }

    is_created_ = false;
}

Quad::Quad(Device* device) {
    vertex_buffer_ = device->create_vertex_buffer();
    Vector3 positions[4] = { {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f} };
    vertex_buffer_->add_vertex_stream(VertexAttributeType::Enum::Position, 4, sizeof(Vector3), (const void*)&positions[0]);
    Vector2 uvs[4] = { {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f} };
    vertex_buffer_->add_vertex_stream(VertexAttributeType::Enum::TexCoord0, 4, sizeof(Vector2), (const void*)&uvs[0]);
    Vector4 colors[4] = { {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} };
    vertex_buffer_->add_vertex_stream(VertexAttributeType::Enum::Color0, 4, sizeof(Vector4), (const void*)&colors[0]);
    vertex_buffer_->upload_data();

    // Setup indices
    std::vector<uint16_t> indices{ 0, 1, 2, 2, 3, 0 };
    uint32_t indices_count = static_cast<uint32_t>(indices.size());
    uint32_t indices_bytes = indices_count * sizeof(uint16_t);
    index_buffer_ = device->create_index_buffer(indices.data(), indices_count);
}

Quad::~Quad() {
}

}// namespace ocarina