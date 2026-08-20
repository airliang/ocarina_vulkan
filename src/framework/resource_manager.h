#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/resources/buffer.h"
#include "rhi/device.h"
#include "bindless_texture_registry.h"
#include "material.h"
#include <mutex>

namespace ocarina {

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
    Material* create_unique_material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader);
    Material* get_material(handle_ty vertex_shader, handle_ty pixel_shader) const noexcept;
    bool release_material(handle_ty vertex_shader, handle_ty pixel_shader);

    Mesh* create_mesh(const std::string& name);
    Mesh* get_mesh(const std::string& name) const noexcept;
    [[nodiscard]] Mesh* get_mesh(uint32_t mesh_id) const noexcept;
    void add_mesh(const std::string& name, Mesh* mesh);

    /// Assign a stable mesh id (called from Mesh construction).
    uint32_t register_mesh(Mesh* mesh);
    void unregister_mesh(Mesh* mesh);

    /// Create a GPU buffer owned by this manager. Returns a TypedBuffer handle for callers.
    template<typename T = std::byte>
    [[nodiscard]] TypedBuffer<T> create_buffer(
        Device* device,
        size_t element_count,
        GraphicBufferBindFlags bind_flags,
        const std::string& name = "");

    [[nodiscard]] Buffer* get_buffer(handle_ty handle) const noexcept;
    /// Destroy the Buffer registered under @p handle. Safe to call with 0 / unknown handles.
    bool release_buffer(handle_ty handle);

    /// Async GPU create: reserves bindless index immediately and enqueues upload.
    [[nodiscard]] TextureHandle create_texture(
        Device* device,
        const Image& image,
        const TextureViewCreation& texture_view,
        const TextureSampler& sampler);
    [[nodiscard]] TextureHandle create_texture(
        Device* device,
        const std::string& name,
        uint32_t width,
        uint32_t height,
        PixelStorage pixel_storage,
        const TextureViewCreation& texture_view,
        const TextureSampler& sampler,
        const void* data = nullptr);
    /// Render targets are created immediately (needed for framebuffer setup).
    [[nodiscard]] Texture* create_render_target_texture(
        Device* device,
        const std::string& name,
        uint32_t width,
        uint32_t height,
        PixelStorage pixel_storage,
        TextureUsageFlags usage);

    [[nodiscard]] TextureHandle get_texture_handle(
        const std::string& name,
        const TextureViewCreation& texture_view,
        const TextureSampler& sampler) const noexcept;
    [[nodiscard]] Texture* get_texture(
        const std::string& name,
        const TextureViewCreation& texture_view,
        const TextureSampler& sampler) const noexcept;

    /// Called from the GPU resource thread when a texture finishes creating.
    void complete_texture(uint64_t key, Texture* texture);

private:
    std::unordered_map<uint64_t, Material*> materials_;
    std::vector<Material*> unique_materials_;
    std::unordered_map<uint64_t, Mesh*> meshes_;
    std::vector<Mesh*> meshes_by_id_;
    std::unordered_map<Mesh*, uint32_t> mesh_to_id_;
    std::unordered_map<uint64_t, TextureHandle> textures_;
    std::unordered_map<handle_ty, Buffer*> buffers_;
    mutable std::mutex mutex_;
};

template<typename T>
TypedBuffer<T> ResourceManager::create_buffer(
    Device* device,
    size_t element_count,
    GraphicBufferBindFlags bind_flags,
    const std::string& name) {
    static_assert(is_valid_buffer_element_v<T>);
    if (device == nullptr || element_count == 0) {
        return {};
    }

    const size_t byte_count = element_count * sizeof(T);
    const handle_ty handle = device->create_buffer(byte_count, bind_flags, name);
    if (handle == 0) {
        return {};
    }

    Buffer* buffer = reinterpret_cast<Buffer*>(handle);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffers_.emplace(handle, buffer);
    }
    return TypedBuffer<T>(handle, element_count);
}

}// namespace ocarina
