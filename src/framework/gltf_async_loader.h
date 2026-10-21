#pragma once

#include "async_loader.h"
#include "bounding_box.h"
#include "math/basic_types.h"
#include "scene.h"
#include "mesh_geometry.h"
#include "material.h"
#include "material_asset.h"
#include "bindless_texture_registry.h"
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
class ShaderProgram;
class Renderer;

class GltfAsyncLoader : public AsyncLoader {
public:
    /// @p renderer must already have loaded a RenderPipelineAsset (Opaque / Transparent passes).
    /// @p vertex_shader / @p pixel_shader are absolute paths used when building MaterialAssets.
    GltfAsyncLoader(
        enki::TaskScheduler* scheduler,
        Device* device,
        Renderer* renderer,
        ocarina_string vertex_shader,
        ocarina_string pixel_shader,
        const ocarina_string& gltf_file);

    ~GltfAsyncLoader() noexcept;

    [[nodiscard]] Scene& get_scene() noexcept { return scene_; }
    [[nodiscard]] const Scene& get_scene() const noexcept { return scene_; }

    /// Clear scene entities / GPU meshes / materials for @p gltf_file and allow load() again.
    void reset_for_reload(const ocarina_string& gltf_file);

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
        Mesh* mesh);
    /// Build MaterialAsset from glTF, then create engine Material via MaterialAsset.
    [[nodiscard]] Material* create_material(const tinygltf::Material& material, const tinygltf::Model& model);
    [[nodiscard]] Material* create_default_material();
    /// @p alpha_blend forces BLEND when true; otherwise alphaMode comes from @p material
    /// (OPAQUE / MASK / BLEND). MASK materials participate in DepthPrepass + Opaque.
    [[nodiscard]] MaterialAsset convert_gltf_material_to_asset(
        const tinygltf::Material* material,
        bool alpha_blend) const;
    [[nodiscard]] Mesh* get_or_create_mesh(const tinygltf::Primitive& gltf_primitive, const tinygltf::Model& model);
    [[nodiscard]] TextureHandle load_gltf_image(int image_index, const tinygltf::Model& model, bool srgb);
    [[nodiscard]] static uint64_t make_geometry_key(const tinygltf::Primitive& primitive);

    ocarina_string gltf_file_;
    fs::path gltf_directory_;
    Renderer* renderer_ = nullptr;
    ocarina_string vertex_shader_;
    ocarina_string pixel_shader_;
    std::unique_ptr<tinygltf::Model> gltf_model_;
    bool gltf_model_parsed_ = false;
    bool gltf_parse_success_ = false;
    ocarina_string gltf_parse_error_;
    ocarina_vector<Mesh*> mesh_storage_;
    ocarina_vector<Material*> material_storage_;
    ocarina_unordered_map<int, TextureHandle> image_textures_;
    ocarina_unordered_map<uint64_t, Mesh*> geometry_meshes_;
    Scene scene_;
    bool is_loaded_ = false;
};

}// namespace ocarina
