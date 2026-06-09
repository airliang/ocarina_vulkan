#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "math/basic_types.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include "rhi/device.h"

namespace tinygltf {
class Model;
class Node;
class Primitive;
class Material;
class Image;
}// namespace tinygltf

namespace ocarina {

class Material;
class Mesh;
class Texture;
class Primitive;
class VertexBuffer;
class IndexBuffer;

class GltfAsyncLoader : public enki::IPinnedTask {
public:
    GltfAsyncLoader(const std::string& gltf_file, Device* device, Material* shared_material);

    ~GltfAsyncLoader() noexcept;

    void Execute() override;

    [[nodiscard]] const std::vector<Primitive*>& get_primitives() const { return primitives_; }

private:
    bool load_gltf_file();
    void load_gltf_node(const tinygltf::Node& node, const tinygltf::Model& model, const float4x4& parent_transform);
    void load_vertex_attributes(VertexBuffer* vb, const tinygltf::Primitive& primitive, const tinygltf::Model& model);
    IndexBuffer* load_index_buffer(const tinygltf::Primitive& primitive, const tinygltf::Model& model);
    void load_material(Primitive* prim, const tinygltf::Material& material, const tinygltf::Model& model);
    Texture* load_gltf_image(int image_index, const tinygltf::Model& model);
    [[nodiscard]] static uint64_t make_geometry_key(const tinygltf::Primitive& primitive);

    std::string gltf_file_;
    fs::path gltf_directory_;
    Device* device_ = nullptr;
    Material* shared_material_ = nullptr;
    std::vector<Primitive*> primitives_;
    std::vector<Primitive*> primitive_storage_;
    std::vector<Mesh*> mesh_storage_;
    std::unordered_map<int, Texture*> image_textures_;
    std::unordered_map<int, IndexBuffer*> index_buffer_cache_;
    std::unordered_map<uint64_t, Mesh*> geometry_meshes_;
    bool is_loaded_ = false;
};

}// namespace ocarina
