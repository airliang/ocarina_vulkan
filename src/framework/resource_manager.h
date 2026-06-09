#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include <mutex>

namespace ocarina {

class Material;
class Mesh;
class Texture;
class Image;
class Device;

class OC_FRAMEWORK_API ResourceManager : public concepts::Noncopyable {
public:
    ~ResourceManager();

    static ResourceManager& instance();

    void cleanup();

    static uint64_t make_material_key(handle_ty vertex_shader, handle_ty pixel_shader) noexcept;
    static uint64_t make_texture_key(const std::string& name, const TextureViewCreation& texture_view, const TextureSampler& sampler) noexcept;

    Material* create_material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader);
    Material* get_material(handle_ty vertex_shader, handle_ty pixel_shader) const noexcept;
    bool release_material(handle_ty vertex_shader, handle_ty pixel_shader);

    Mesh* create_mesh(Device* device, const std::string& name);
    Mesh* get_mesh(const std::string& name) const noexcept;
    void add_mesh(const std::string& name, Mesh* mesh);

    Texture* create_texture(Device* device, const Image& image, const TextureViewCreation& texture_view, const TextureSampler& sampler);
    Texture* create_texture(Device* device, const std::string& name, uint32_t width, uint32_t height, PixelStorage pixel_storage,
                            const TextureViewCreation& texture_view, const TextureSampler& sampler, const void* data = nullptr);
    Texture* get_texture(const std::string& name, const TextureViewCreation& texture_view, const TextureSampler& sampler) const noexcept;
private:

    std::unordered_map<uint64_t, Material*> materials_;
    std::unordered_map<uint64_t, Mesh*> meshes_;
    std::unordered_map<uint64_t, Texture*> textures_;
    std::mutex mutex_;
};

}// namespace ocarina