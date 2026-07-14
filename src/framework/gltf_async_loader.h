#pragma once

#include "async_loader.h"
#include "bounding_box.h"
#include "math/basic_types.h"
#include "scene.h"
#include "mesh_geometry.h"
#include "rhi/device.h"
#include <memory>

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

class GltfAsyncLoader : public AsyncLoader {
public:
    GltfAsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        std::vector<ShaderCompileTask::Entry>* shader_entries,
        const std::string& gltf_file);

    ~GltfAsyncLoader() noexcept;

    [[nodiscard]] Scene& get_scene() noexcept { return scene_; }
    [[nodiscard]] const Scene& get_scene() const noexcept { return scene_; }

protected:
    void load(Device* device) override;
    [[nodiscard]] uint32_t count_load_progress_steps() override;

private:
    [[nodiscard]] bool ensure_gltf_parsed();
    void begin_gltf_progress();
    bool load_gltf_file();
    void build_scene_clusters();
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
    std::unique_ptr<tinygltf::Model> gltf_model_;
    bool gltf_model_parsed_ = false;
    bool gltf_parse_success_ = false;
    std::string gltf_parse_error_;
    handle_ty vertex_shader_ = InvalidUI64;
    handle_ty pixel_shader_ = InvalidUI64;
    std::vector<Mesh*> mesh_storage_;
    std::unordered_map<int, Texture*> image_textures_;
    std::unordered_map<uint64_t, Mesh*> geometry_meshes_;
    Scene scene_;
    bool is_loaded_ = false;
};

}// namespace ocarina
