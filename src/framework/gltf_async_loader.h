#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "TaskScheduler.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "ext/tinygltf/tiny_gltf.h"

#include "primitive.h"
#include "rhi/device.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"

namespace ocarina {


class GltfAsyncLoader : public enki::IPinnedTask
{
public:

    GltfAsyncLoader(const std::string& gltf_file, Device* device)
    {
        gltf_file_ = std::move(gltf_file);
        device_ = device;
    }

    void Execute() override;

    const std::vector<Primitive*>& get_primitives() const { return primitives_; }

private:
    bool load_gltf_file();
    void load_gltf_node(const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model);
    void load_vertex_attributes(VertexBuffer* vb, const tinygltf::Primitive& primitive, const tinygltf::Model& model);
    IndexBuffer* load_index_buffer(const tinygltf::Primitive& primitive, const tinygltf::Model& model);
    void load_material(Primitive* prim, const tinygltf::Material& material, const tinygltf::Model& model);
    std::string gltf_file_;
    Device* device_;
    std::vector<Primitive*> primitives_;
    bool is_loaded_ = false;
};

}// namespace ocarina