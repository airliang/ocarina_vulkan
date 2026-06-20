#pragma once

#include "resource_manager.h"
#include "core/logging.h"
#include "material.h"
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
    for (auto& [key, mesh] : meshes_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    meshes_.clear();
    for (auto& [key, texture] : textures_) {
        if (texture) {
            texture->destroy();
            ocarina::delete_with_allocator<Texture>(texture);
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

void ResourceManager::add_mesh(const std::string& name, Mesh* mesh) {
    Mesh* existing_mesh = get_mesh(name);
    if (existing_mesh) {
        if (existing_mesh == mesh) {
            return; // Same mesh already exists, do nothing
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
    uint64_t key = make_texture_key(name, texture_view, sampler);
    auto it = textures_.find(key);
    return it != textures_.end() ? it->second : nullptr;
}

Texture* ResourceManager::create_texture(Device* device, const Image& image, const TextureViewCreation& texture_view, const TextureSampler& sampler) {
    std::string image_name = image.name();
    uint64_t key = make_texture_key(image_name, texture_view, sampler);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    Texture* texture = ocarina::new_with_allocator<Texture>(device->impl(), const_cast<Image*>(&image), texture_view, sampler);
    std::lock_guard<std::mutex> l{ mutex_ };
    textures_.emplace(key, texture);
    return texture;
}

Texture* ResourceManager::create_texture(Device* device, const std::string& name, uint32_t width, uint32_t height,
                                         PixelStorage pixel_storage, const TextureViewCreation& texture_view,
                                         const TextureSampler& sampler, const void* data) {
    uint64_t key = make_texture_key(name, texture_view, sampler);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    Texture* texture = ocarina::new_with_allocator<Texture>(
        device->impl(), width, height, 1, pixel_storage, texture_view, sampler, uint4(0, 0, 0, 255), data);
    std::lock_guard<std::mutex> l{ mutex_ };
    textures_.emplace(key, texture);
    return texture;
}

}// namespace ocarina