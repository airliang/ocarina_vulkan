#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "math/basic_types.h"
#include "scene.h"
#include "mesh_geometry.h"
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

class GltfAsyncLoader : public enki::IPinnedTask {
public:
    GltfAsyncLoader(const std::string& gltf_file, Device* device, Material* shared_material);

    ~GltfAsyncLoader() noexcept;

    void Execute() override;

    [[nodiscard]] Scene& get_scene() noexcept { return scene_; }
    [[nodiscard]] const Scene& get_scene() const noexcept { return scene_; }
    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

private:
    void build_scene_clusters();
    bool load_gltf_file();
    void load_gltf_node(const tinygltf::Node& node, const tinygltf::Model& model, const float4x4& parent_transform);
    [[nodiscard]] BoundingBox append_primitive_geometry(
        const tinygltf::Primitive& primitive,
        const tinygltf::Model& model,
        MeshGeometrySlice& out_slice);
    void load_material(Primitive& prim, const tinygltf::Material& material, const tinygltf::Model& model);
    Texture* load_gltf_image(int image_index, const tinygltf::Model& model);
    [[nodiscard]] static uint64_t make_geometry_key(const tinygltf::Primitive& primitive);

    std::string gltf_file_;
    fs::path gltf_directory_;
    Device* device_ = nullptr;
    Material* shared_material_ = nullptr;
    std::vector<Mesh*> mesh_storage_;
    std::unordered_map<int, Texture*> image_textures_;
    std::unordered_map<uint64_t, Mesh*> geometry_meshes_;
    Scene scene_;
    bool is_loaded_ = false;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina
