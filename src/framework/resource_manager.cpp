#include "resource_manager.h"
#include "gpu_resource_thread.h"
#include "bindless_texture_registry.h"
#include "core/logging.h"
#include "mesh.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/device.h"
#include "rhi/shader_program.h"
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
    // Release materials first so they can release their TypedBuffers while Device is alive.
    for (auto& [key, material] : materials_) {
        ocarina::delete_with_allocator<Material>(material);
    }
    materials_.clear();
    for (Material* material : unique_materials_) {
        ocarina::delete_with_allocator<Material>(material);
    }
    unique_materials_.clear();
    for (auto& [key, program] : shader_programs_) {
        if (cached_device_ != nullptr) {
            cached_device_->release_shader_program(program);
        }
        ocarina::delete_with_allocator<ShaderProgram>(program);
    }
    shader_programs_.clear();
    cached_device_ = nullptr;
    for (auto& [key, mesh] : meshes_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    meshes_.clear();
    meshes_by_id_.clear();
    mesh_to_id_.clear();
    for (auto& [key, handle] : textures_) {
        if (handle.bindless_index_ != InvalidUI32) {
            BindlessTextureRegistry::instance().release_index(handle.bindless_index_);
        }
        if (handle.texture_ != nullptr) {
            handle.texture_->destroy();
            ocarina::delete_with_allocator<Texture>(handle.texture_);
        }
    }
    textures_.clear();

    // Any remaining registered buffers (e.g. FrameResources forgot to release).
    std::unordered_map<handle_ty, Buffer*> remaining_buffers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remaining_buffers.swap(buffers_);
    }
    for (auto& [handle, buffer] : remaining_buffers) {
        if (buffer != nullptr && buffer->device() != nullptr) {
            buffer->device()->destroy_buffer(handle);
        }
    }
}

Buffer* ResourceManager::get_buffer(handle_ty handle) const noexcept {
    if (handle == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = buffers_.find(handle);
    return it != buffers_.end() ? it->second : nullptr;
}

bool ResourceManager::release_buffer(handle_ty handle) {
    if (handle == 0) {
        return false;
    }

    Buffer* buffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = buffers_.find(handle);
        if (it == buffers_.end()) {
            return false;
        }
        buffer = it->second;
        buffers_.erase(it);
    }

    if (buffer != nullptr && buffer->device() != nullptr) {
        buffer->device()->destroy_buffer(handle);
        return true;
    }
    return false;
}

uint64_t ResourceManager::make_material_key(ShaderProgram* shader_program) noexcept {
    return hash64(reinterpret_cast<uint64_t>(shader_program));
}

uint64_t ResourceManager::make_texture_key(
    const std::string& name,
    const TextureViewCreation& texture_view,
    const TextureSampler& sampler) noexcept {
    return hash64(
        name,
        texture_view.mip_level_count,
        texture_view.usage,
        sampler.filter(),
        sampler.u_address(),
        sampler.v_address(),
        sampler.w_address());
}

ShaderProgramKey ResourceManager::make_graphics_program_key(
    const std::string& vertex_shader_file,
    const std::string& pixel_shader_file,
    const std::set<std::string>& vertex_options,
    const std::set<std::string>& pixel_options,
    const std::string& entry_point) {
    ShaderProgramKey key;
    key.vertex_shader_file = vertex_shader_file;
    key.pixel_shader_file = pixel_shader_file;
    key.vertex_options = vertex_options;
    key.pixel_options = pixel_options;
    key.entry_point = entry_point;
    return key;
}

ShaderProgramKey ResourceManager::make_compute_program_key(
    const std::string& compute_shader_file,
    const std::set<std::string>& options,
    const std::string& entry_point) {
    ShaderProgramKey key;
    key.compute_shader_file = compute_shader_file;
    key.compute_options = options;
    key.entry_point = entry_point;
    return key;
}

ShaderProgram* ResourceManager::create_shader_program(
    Device* device,
    const std::string& vertex_shader_file,
    const std::string& pixel_shader_file,
    const std::set<std::string>& vertex_options,
    const std::set<std::string>& pixel_options,
    const std::string& entry_point) {
    const ShaderProgramKey key = make_graphics_program_key(
        vertex_shader_file,
        pixel_shader_file,
        vertex_options,
        pixel_options,
        entry_point);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = shader_programs_.find(key);
        if (it != shader_programs_.end()) {
            return it->second;
        }
    }

    ShaderProgram* program = ShaderProgram::compile_graphics_from_HLSL(
        vertex_shader_file,
        pixel_shader_file,
        vertex_options,
        pixel_options,
        entry_point);
    if (program == nullptr) {
        return nullptr;
    }

    program->create_descriptor_set_layouts(device);

    std::lock_guard<std::mutex> lock(mutex_);
    if (cached_device_ == nullptr) {
        cached_device_ = device;
    }
    const auto [it, inserted] = shader_programs_.emplace(key, program);
    if (!inserted) {
        if (cached_device_ != nullptr) {
            cached_device_->release_shader_program(program);
        }
        ocarina::delete_with_allocator<ShaderProgram>(program);
        return it->second;
    }
    return program;
}

ShaderProgram* ResourceManager::get_shader_program(
    const std::string& vertex_shader_file,
    const std::string& pixel_shader_file,
    const std::set<std::string>& vertex_options,
    const std::set<std::string>& pixel_options,
    const std::string& entry_point) const noexcept {
    const ShaderProgramKey key = make_graphics_program_key(
        vertex_shader_file,
        pixel_shader_file,
        vertex_options,
        pixel_options,
        entry_point);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = shader_programs_.find(key);
    return it != shader_programs_.end() ? it->second : nullptr;
}

ShaderProgram* ResourceManager::create_compute_shader_program(
    Device* device,
    const std::string& compute_shader_file,
    const std::set<std::string>& options,
    const std::string& entry_point) {
    const ShaderProgramKey key = make_compute_program_key(compute_shader_file, options, entry_point);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = shader_programs_.find(key);
        if (it != shader_programs_.end()) {
            return it->second;
        }
    }

    ShaderProgram* program = ShaderProgram::compile_compute_from_HLSL(
        compute_shader_file,
        options,
        entry_point);
    if (program == nullptr) {
        return nullptr;
    }

    program->create_descriptor_set_layouts(device);

    std::lock_guard<std::mutex> lock(mutex_);
    if (cached_device_ == nullptr) {
        cached_device_ = device;
    }
    const auto [it, inserted] = shader_programs_.emplace(key, program);
    if (!inserted) {
        if (cached_device_ != nullptr) {
            cached_device_->release_shader_program(program);
        }
        ocarina::delete_with_allocator<ShaderProgram>(program);
        return it->second;
    }
    return program;
}

ShaderProgram* ResourceManager::get_compute_shader_program(
    const std::string& compute_shader_file,
    const std::set<std::string>& options,
    const std::string& entry_point) const noexcept {
    const ShaderProgramKey key = make_compute_program_key(compute_shader_file, options, entry_point);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = shader_programs_.find(key);
    return it != shader_programs_.end() ? it->second : nullptr;
}

Material* ResourceManager::create_material(Device* device, ShaderProgram* shader_program) {
    uint64_t key = make_material_key(shader_program);
    auto it = materials_.find(key);
    if (it != materials_.end()) {
        return it->second;
    }

    Material* material = ocarina::new_with_allocator<Material>(device, shader_program);
    materials_.emplace(key, material);
    return material;
}

Material* ResourceManager::create_unique_material(Device* device, ShaderProgram* shader_program) {
    Material* material = ocarina::new_with_allocator<Material>(device, shader_program);
    unique_materials_.push_back(material);
    return material;
}

Material* ResourceManager::get_material(ShaderProgram* shader_program) const noexcept {
    uint64_t key = make_material_key(shader_program);
    auto it = materials_.find(key);
    return it != materials_.end() ? it->second : nullptr;
}

bool ResourceManager::release_material(ShaderProgram* shader_program) {
    uint64_t key = make_material_key(shader_program);
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

TextureHandle ResourceManager::get_texture_handle(
    const std::string& name,
    const TextureViewCreation& texture_view,
    const TextureSampler& sampler) const noexcept {
    uint64_t key = make_texture_key(name, texture_view, sampler);
    std::lock_guard<std::mutex> l{mutex_};
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        return TextureHandle{};
    }
    return it->second;
}

namespace {

void destroy_texture(Texture* texture) {
    if (texture == nullptr) {
        return;
    }
    texture->destroy();
    ocarina::delete_with_allocator<Texture>(texture);
}

void rollback_texture_creation(uint32_t bindless_index, Texture* texture) {
    if (bindless_index != InvalidUI32) {
        BindlessTextureRegistry::instance().release_index(bindless_index);
    }
    destroy_texture(texture);
}

} // namespace

TextureHandle ResourceManager::create_texture(
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
    Texture* texture = ocarina::new_with_allocator<Texture>(
        device->impl(),
        image.width(),
        image.height(),
        1u,
        image.pixel_storage(),
        texture_view,
        sampler,
        uint4(0, 0, 0, 255),
        nullptr);
    if (texture == nullptr) {
        rollback_texture_creation(bindless_index, nullptr);
        return {};
    }

    TextureHandle handle{bindless_index, texture};
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto [it, inserted] = textures_.emplace(key, handle);
        if (!inserted) {
            rollback_texture_creation(bindless_index, texture);
            return it->second;
        }
    }

    auto request = std::make_shared<TextureGPUResourceRequest>(device, texture);
    request->kind = GPUResourceRequestType::TextureFromData;
    request->name = std::move(image_name);
    request->bindless_index = bindless_index;
    const size_t byte_count = image.size_in_bytes();
    const uint8_t* src = image.pixel_ptr<uint8_t>();
    if (src != nullptr && byte_count > 0) {
        request->pixel_data.assign(src, src + byte_count);
    }

    GPUResourceThread::instance().enqueue(std::move(request));
    return handle;
}

TextureHandle ResourceManager::create_texture(
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
    Texture* texture = ocarina::new_with_allocator<Texture>(
        device->impl(),
        width,
        height,
        1u,
        pixel_storage,
        texture_view,
        sampler,
        uint4(0, 0, 0, 255),
        nullptr);
    if (texture == nullptr) {
        rollback_texture_creation(bindless_index, nullptr);
        return {};
    }

    TextureHandle handle{bindless_index, texture};
    {
        std::lock_guard<std::mutex> l{mutex_};
        auto [it, inserted] = textures_.emplace(key, handle);
        if (!inserted) {
            rollback_texture_creation(bindless_index, texture);
            return it->second;
        }
    }

    auto request = std::make_shared<TextureGPUResourceRequest>(device, texture);
    request->kind = GPUResourceRequestType::TextureFromData;
    request->name = name;
    request->bindless_index = bindless_index;
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

    Texture* texture = ocarina::new_with_allocator<Texture>(
        device->impl(),
        width,
        height,
        pixel_storage,
        usage);
    if (texture == nullptr) {
        return nullptr;
    }

    const bool bindless =
        (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0;
    uint32_t bindless_index = InvalidUI32;
    if (bindless) {
        bindless_index = BindlessTextureRegistry::instance().allocate_slot();
    }

    auto request = std::make_shared<TextureGPUResourceRequest>(device, texture);
    request->kind = GPUResourceRequestType::RenderTarget;
    request->name = name;
    request->bindless_index = bindless_index;
    request->process();

    std::lock_guard<std::mutex> l{mutex_};
    auto it = textures_.find(key);
    if (it != textures_.end() && it->second.texture_ != nullptr) {
        if (bindless_index != InvalidUI32) {
            BindlessTextureRegistry::instance().release_index(bindless_index);
        }
        destroy_texture(texture);
        return it->second.texture_;
    }
    textures_[key] = TextureHandle{bindless_index, texture};
    return texture;
}

}// namespace ocarina
