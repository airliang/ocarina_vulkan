#include "resource_manager.h"
#include "gpu_resource_thread.h"
#include "bindless_texture_registry.h"
#include "core/logging.h"
#include "mesh.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/device.h"
#include "core/image.h"

namespace ocarina {

ResourceManager::~ResourceManager() {
    cleanup();
}

ResourceManager& ResourceManager::instance() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::cleanup() {
    for (auto& [key, material] : materials_) {
        ocarina::delete_with_allocator<Material>(material);
    }
    materials_.clear();
    for (Material* material : unique_materials_) {
        ocarina::delete_with_allocator<Material>(material);
    }
    unique_materials_.clear();
    for (auto& [key, mesh] : meshes_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    meshes_.clear();
    meshes_by_id_.clear();
    mesh_to_id_.clear();
    for (auto& [key, handle] : textures_) {
        if (handle.texture_ != nullptr) {
            handle.texture_->destroy();
            ocarina::delete_with_allocator<Texture>(handle.texture_);
        }
    }
    textures_.clear();
}

uint64_t ResourceManager::make_material_key(handle_ty vertex_shader, handle_ty pixel_shader) noexcept {
    return hash64(vertex_shader, pixel_shader);
}

uint64_t ResourceManager::make_texture_key(const std::string& name, const TextureViewCreation& texture_view, const TextureSampler& sampler) noexcept {
    return hash64(name, texture_view.mip_level_count, texture_view.usage, sampler.filter(), sampler.u_address(), sampler.v_address(), sampler.w_address());
}

Material* ResourceManager::create_material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) {
    uint64_t key = make_material_key(vertex_shader, pixel_shader);
    auto it = materials_.find(key);
    if (it != materials_.end()) {
        return it->second;
    }

    Material* material = ocarina::new_with_allocator<Material>(device, vertex_shader, pixel_shader);
    materials_.emplace(key, material);
    return material;
}

Material* ResourceManager::create_unique_material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) {
    Material* material = ocarina::new_with_allocator<Material>(device, vertex_shader, pixel_shader);
    unique_materials_.push_back(material);
    return material;
}

Material* ResourceManager::get_material(handle_ty vertex_shader, handle_ty pixel_shader) const noexcept {
    uint64_t key = make_material_key(vertex_shader, pixel_shader);
    auto it = materials_.find(key);
    return it != materials_.end() ? it->second : nullptr;
}

bool ResourceManager::release_material(handle_ty vertex_shader, handle_ty pixel_shader) {
    uint64_t key = make_material_key(vertex_shader, pixel_shader);
    auto it = materials_.find(key);
    if (it == materials_.end()) {
        return false;
    }

    delete it->second;
    materials_.erase(it);
    return true;
}

Mesh* ResourceManager::create_mesh(const std::string& name) {
    auto it = meshes_.find(hash64(name));
    if (it != meshes_.end()) {
        return it->second;
    }
    Mesh* mesh = nullptr;
    if (name == "quad") {
        mesh = get_mesh(name);
        if (!mesh) {
            mesh = Mesh::create_quad();
        }
    }
    if (mesh) {
        std::lock_guard<std::mutex> l{ mutex_ };
        meshes_.emplace(hash64(name), mesh);
    }
    return mesh;
}

Mesh* ResourceManager::get_mesh(const string& name) const noexcept {
    auto it = meshes_.find(hash64(name));
    return it != meshes_.end() ? it->second : nullptr;
}

Mesh* ResourceManager::get_mesh(uint32_t mesh_id) const noexcept {
    if (mesh_id == InvalidUI32) {
        return nullptr;
    }
    std::lock_guard<std::mutex> l{mutex_};
    if (mesh_id >= meshes_by_id_.size()) {
        return nullptr;
    }
    return meshes_by_id_[mesh_id];
}

uint32_t ResourceManager::register_mesh(Mesh* mesh) {
    if (mesh == nullptr) {
        return InvalidUI32;
    }
    std::lock_guard<std::mutex> l{mutex_};
    auto it = mesh_to_id_.find(mesh);
    if (it != mesh_to_id_.end()) {
        return it->second;
    }
    const uint32_t id = static_cast<uint32_t>(meshes_by_id_.size());
    meshes_by_id_.push_back(mesh);
    mesh_to_id_.insert(std::make_pair(mesh, id));
    return id;
}

void ResourceManager::unregister_mesh(Mesh* mesh) {
    if (mesh == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> l{mutex_};
    auto it = mesh_to_id_.find(mesh);
    if (it == mesh_to_id_.end()) {
        return;
    }
    const uint32_t id = it->second;
    if (id < meshes_by_id_.size()) {
        meshes_by_id_[id] = nullptr;
    }
    mesh_to_id_.erase(it);
}

void ResourceManager::add_mesh(const std::string& name, Mesh* mesh) {
    Mesh* existing_mesh = get_mesh(name);
    if (existing_mesh) {
        if (existing_mesh == mesh) {
            return;
        }
        else {
            OC_ERROR("Mesh with name '{}' already exists. Cannot add a different mesh with the same name.", name);
            return;
        }
    }
    std::lock_guard<std::mutex> l{ mutex_ };
    meshes_.emplace(hash64(name), mesh);
}

Texture* ResourceManager::get_texture(const std::string& name, const TextureViewCreation& texture_view, const TextureSampler& sampler) const noexcept {
    return get_texture_handle(name, texture_view, sampler).texture_;
}

Material::TextureHandle ResourceManager::get_texture_handle(
    const std::string& name,
    const TextureViewCreation& texture_view,
    const TextureSampler& sampler) const noexcept {
    uint64_t key = make_texture_key(name, texture_view, sampler);
    std::lock_guard<std::mutex> l{mutex_};
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        return Material::TextureHandle{};
    }
    Material::TextureHandle handle = it->second;
    if (handle.texture_ == nullptr && handle.bindless_index_ != InvalidUI32) {
        handle.texture_ = BindlessTextureRegistry::instance().get_texture(handle.bindless_index_);
    }
    return handle;
}

void ResourceManager::complete_texture(uint64_t key, Texture* texture) {
    std::lock_guard<std::mutex> l{mutex_};
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }
    it->second.texture_ = texture;
}

Material::TextureHandle ResourceManager::create_texture(
    Device* device,
    const Image& image,
    const TextureViewCreation& texture_view,
    const TextureSampler& sampler) {
    std::string image_name = image.name();
    uint64_t key = make_texture_key(image_name, texture_view, sampler);
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto it = textures_.find(key);
        if (it != textures_.end()) {
            return it->second;
        }
    }

    const uint32_t bindless_index = BindlessTextureRegistry::instance().allocate_slot();
    Material::TextureHandle handle{bindless_index, nullptr};
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto [it, inserted] = textures_.emplace(key, handle);
        if (!inserted) {
            return it->second;
        }
    }

    auto request = std::make_shared<TextureGPUResourceRequest>();
    request->kind = GPUResourceRequestType::TextureFromData;
    request->device = device;
    request->name = std::move(image_name);
    request->width = image.width();
    request->height = image.height();
    request->depth = 1;
    request->pixel_storage = image.pixel_storage();
    request->texture_view = texture_view;
    request->sampler = sampler;
    request->bindless_index = bindless_index;
    request->cache_key = key;
    request->has_cache_key = true;
    const size_t byte_count = image.size_in_bytes();
    const uint8_t* src = image.pixel_ptr<uint8_t>();
    if (src != nullptr && byte_count > 0) {
        request->pixel_data.assign(src, src + byte_count);
    }

    GPUResourceThread::instance().enqueue(std::move(request));
    return handle;
}

Material::TextureHandle ResourceManager::create_texture(
    Device* device,
    const std::string& name,
    uint32_t width,
    uint32_t height,
    PixelStorage pixel_storage,
    const TextureViewCreation& texture_view,
    const TextureSampler& sampler,
    const void* data) {
    uint64_t key = make_texture_key(name, texture_view, sampler);
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto it = textures_.find(key);
        if (it != textures_.end()) {
            return it->second;
        }
    }

    const uint32_t bindless_index = BindlessTextureRegistry::instance().allocate_slot();
    Material::TextureHandle handle{bindless_index, nullptr};
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto [it, inserted] = textures_.emplace(key, handle);
        if (!inserted) {
            return it->second;
        }
    }

    auto request = std::make_shared<TextureGPUResourceRequest>();
    request->kind = GPUResourceRequestType::TextureFromData;
    request->device = device;
    request->name = name;
    request->width = width;
    request->height = height;
    request->depth = 1;
    request->pixel_storage = pixel_storage;
    request->texture_view = texture_view;
    request->sampler = sampler;
    request->bindless_index = bindless_index;
    request->cache_key = key;
    request->has_cache_key = true;
    if (data != nullptr) {
        const size_t byte_count = static_cast<size_t>(width) * height * pixel_size(pixel_storage);
        const auto* src = static_cast<const uint8_t*>(data);
        request->pixel_data.assign(src, src + byte_count);
    }

    GPUResourceThread::instance().enqueue(std::move(request));
    return handle;
}

Texture* ResourceManager::create_render_target_texture(
    Device* device,
    const std::string& name,
    uint32_t width,
    uint32_t height,
    PixelStorage pixel_storage,
    TextureUsageFlags usage) {
    uint64_t key = hash64(name, width, height, pixel_storage, usage);
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto it = textures_.find(key);
        if (it != textures_.end() && it->second.texture_ != nullptr) {
            return it->second.texture_;
        }
    }

    // Render targets must exist immediately for framebuffer / attachment setup.
    auto request = std::make_shared<TextureGPUResourceRequest>();
    request->kind = GPUResourceRequestType::RenderTarget;
    request->device = device;
    request->name = name;
    request->width = width;
    request->height = height;
    request->pixel_storage = pixel_storage;
    request->usage = usage;

    const bool bindless =
        (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0;
    uint32_t bindless_index = InvalidUI32;
    if (bindless) {
        bindless_index = BindlessTextureRegistry::instance().allocate_slot();
        request->bindless_index = bindless_index;
    }

    Texture* texture = nullptr;
    request->out_texture = &texture;
    request->process();
    if (texture == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> l{mutex_};
    auto it = textures_.find(key);
    if (it != textures_.end() && it->second.texture_ != nullptr) {
        texture->destroy();
        ocarina::delete_with_allocator<Texture>(texture);
        return it->second.texture_;
    }
    textures_[key] = Material::TextureHandle{bindless_index, texture};
    return texture;
}

}// namespace ocarina
