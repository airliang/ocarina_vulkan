#include "gltf_async_loader.h"
#include "enki_task_debug.h"
#include "loading_progress_listener.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "ext/tinygltf/tiny_gltf.h"

#include "core/logging.h"
#include "core/hash.h"
#include "core/image.h"
#include "transform.h"
#include "render_component.h"
#include "mesh.h"
#include "material.h"
#include "math/basic_types.h"
#include "scene.h"
#include "global_gpu_storage.h"
#include "mesh_cpu_memory.h"
#include "bounding_box.h"
#include "resource_manager.h"
#include "rhi/shader_program_key.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/bindless_sampler.h"
#include <chrono>

namespace ocarina {

namespace {

float4x4 node_local_transform(const tinygltf::Node& node) {
    float3 translation = float3(0.0f);
    if (node.translation.size() == 3) {
        translation = float3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2]));
    }

    quaternion rotation = quaternion(0, 0, 0, 1);
    if (node.rotation.size() == 4) {
        rotation = quaternion(
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
            static_cast<float>(node.rotation[3]));
    }

    float3 scale = float3(1.0f);
    if (node.scale.size() == 3) {
        scale = float3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2]));
    }

    Transform<float4x4> transform;
    transform.set_TRS(translation, rotation, scale);
    return transform.mat4x4();
}

uint32_t position_vertex_count(const MeshPositions& positions) {
    return static_cast<uint32_t>(positions.size());
}

struct GltfPixelSource {
    PixelStorage format = PixelStorage::BYTE4;
    const void* data = nullptr;
    ocarina_vector<uint8_t> owned;
    uint32_t width = 0;
    uint32_t height = 0;
};

GltfPixelSource resolve_gltf_image_pixels(const tinygltf::Image& gltf_image, const fs::path& gltf_directory) {
    GltfPixelSource result;
    if (!gltf_image.image.empty() && gltf_image.width > 0 && gltf_image.height > 0) {
        result.width = static_cast<uint32_t>(gltf_image.width);
        result.height = static_cast<uint32_t>(gltf_image.height);

        switch (gltf_image.component) {
        case 4:
            result.format = PixelStorage::BYTE4;
            result.data = gltf_image.image.data();
            return result;
        case 3: {
            const size_t pixel_count = static_cast<size_t>(gltf_image.width * gltf_image.height);
            result.format = PixelStorage::BYTE4;
            result.owned.resize(pixel_count * 4);
            for (size_t i = 0; i < pixel_count; ++i) {
                const size_t src = i * 3;
                const size_t dst = i * 4;
                result.owned[dst + 0] = gltf_image.image[src + 0];
                result.owned[dst + 1] = gltf_image.image[src + 1];
                result.owned[dst + 2] = gltf_image.image[src + 2];
                result.owned[dst + 3] = 255;
            }
            result.data = result.owned.data();
            return result;
        }
        case 2:
            result.format = PixelStorage::BYTE2;
            result.data = gltf_image.image.data();
            return result;
        case 1:
            result.format = PixelStorage::BYTE1;
            result.data = gltf_image.image.data();
            return result;
        default:
            break;
        }
    }

    if (!gltf_image.uri.empty()) {
        // Keep encoded 8-bit bytes. Albedo uses VK *_SRGB views; ORM/normals stay linear UNORM.
        Image image = Image::load(gltf_directory / gltf_image.uri, ColorSpace::LINEAR);
        if (image.pixel_ptr() == nullptr) {
            return result;
        }
        result.width = image.resolution().x;
        result.height = image.resolution().y;
        result.format = image.pixel_storage();
        const auto* bytes = reinterpret_cast<const uint8_t*>(image.pixel_ptr());
        result.owned.assign(bytes, bytes + image.size_in_bytes());
        result.data = result.owned.data();
    }

    return result;
}

double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

uint64_t make_vertex_attributes_key(const tinygltf::Primitive& primitive) {
    uint64_t key = hash64(primitive.indices);
    for (const auto& attr : primitive.attributes) {
        key = hash64(attr.first, attr.second, key);
    }
    return key;
}

uint32_t count_gltf_primitives(const tinygltf::Model& model) {
    uint32_t count = 0;
    for (const tinygltf::Mesh& mesh : model.meshes) {
        count += static_cast<uint32_t>(mesh.primitives.size());
    }
    return count;
}

[[nodiscard]] BoundingBox bounds_from_position_accessor(const tinygltf::Accessor& accessor) {
    BoundingBox bounds;
    if (accessor.minValues.size() >= 3 && accessor.maxValues.size() >= 3) {
        bounds.min = make_float3(
            static_cast<float>(accessor.minValues[0]),
            static_cast<float>(accessor.minValues[1]),
            static_cast<float>(accessor.minValues[2]));
        bounds.max = make_float3(
            static_cast<float>(accessor.maxValues[0]),
            static_cast<float>(accessor.maxValues[1]),
            static_cast<float>(accessor.maxValues[2]));
        bounds.valid = true;
    }
    return bounds;
}

}// namespace

uint64_t GltfAsyncLoader::make_geometry_key(const tinygltf::Primitive& primitive) {
    return make_vertex_attributes_key(primitive);
}

GltfAsyncLoader::GltfAsyncLoader(
    enki::TaskScheduler* scheduler,
    Device* device,
    PSORequest mesh_pso_request,
    const std::string& gltf_file,
    RHIRenderPass* target_render_pass)
    : AsyncLoader(scheduler, device, target_render_pass)
    , gltf_file_(gltf_file)
    , gltf_directory_(fs::path(gltf_file).parent_path()) {
    if (mesh_pso_request.render_pass == nullptr) {
        mesh_pso_request.render_pass = target_render_pass;
    }
    mesh_pso_request_ = mesh_pso_request;
    add_pso_request(std::move(mesh_pso_request));
}

GltfAsyncLoader::~GltfAsyncLoader() noexcept {
    for (Mesh* mesh : mesh_storage_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    mesh_storage_.clear();
}

void GltfAsyncLoader::set_alpha_blend_pso_request(PSORequest request) {
    if (request.render_pass == nullptr) {
        request.render_pass = target_render_pass_;
    }
    // Same shader files as opaque; pixel option selects the ALPHA_BLEND=1 variant.
    if (request.vertex_shader_path.empty()) {
        request.vertex_shader_path = mesh_pso_request_.vertex_shader_path;
    }
    if (request.pixel_shader_path.empty()) {
        request.pixel_shader_path = mesh_pso_request_.pixel_shader_path;
    }
    request.pixel_options.insert(make_shader_option("ALPHA_BLEND", 1));
    if (!request.blend_state.blend_enable) {
        request.blend_state = BlendState::AlphaBlend();
    }
    request.depth_stencil_state.depth_write_enable = false;

    alpha_blend_pso_request_ = request;
    has_alpha_blend_pso_request_ = true;
    add_pso_request(std::move(request));
}

ShaderProgram* GltfAsyncLoader::resolve_shader_program(const PSORequest& request) {
    if (request.has_shader_program()) {
        request.shader_program->ensure_gpu_shaders(device_);
        return request.shader_program;
    }
    if (!request.has_shader_paths() || device_ == nullptr) {
        return nullptr;
    }

    ShaderProgram* program = ResourceManager::instance().create_shader_program(
        device_,
        request.vertex_shader_path,
        request.pixel_shader_path,
        request.vertex_options,
        request.pixel_options);
    if (program != nullptr) {
        program->ensure_gpu_shaders(device_);
    }
    return program;
}

void GltfAsyncLoader::load(Device* device) {
    if (is_loaded_) {
        return;
    }

    device_ = device;
    shader_program_ = resolve_shader_program(mesh_pso_request_);
    if (shader_program_ != nullptr) {
        mesh_pso_request_.shader_program = shader_program_;
    }
    if (has_alpha_blend_pso_request_) {
        alpha_blend_shader_program_ = resolve_shader_program(alpha_blend_pso_request_);
        if (alpha_blend_shader_program_ != nullptr) {
            alpha_blend_pso_request_.shader_program = alpha_blend_shader_program_;
        }
    }

    is_loaded_ = load_gltf_file();
}

bool GltfAsyncLoader::ensure_gltf_parsed() {
    if (gltf_model_parsed_) {
        return gltf_parse_success_;
    }

    gltf_model_parsed_ = true;
    if (!gltf_model_) {
        gltf_model_ = std::make_unique<tinygltf::Model>();
    }

    tinygltf::TinyGLTF gltf_context;
    std::string warning;
    const bool binary = fs::path(gltf_file_).extension() == ".glb";
    gltf_parse_success_ = binary
        ? gltf_context.LoadBinaryFromFile(gltf_model_.get(), &gltf_parse_error_, &warning, gltf_file_)
        : gltf_context.LoadASCIIFromFile(gltf_model_.get(), &gltf_parse_error_, &warning, gltf_file_);

    if (!warning.empty()) {
        OC_WARNING_FORMAT("glTF loader warning for {}: {}", gltf_file_.c_str(), warning.c_str());
    }

    return gltf_parse_success_;
}

uint32_t GltfAsyncLoader::count_load_progress_steps() {
    if (!ensure_gltf_parsed() || gltf_model_ == nullptr || gltf_model_->scenes.empty()) {
        return 0;
    }
    return count_gltf_primitives(*gltf_model_);
}

void GltfAsyncLoader::begin_gltf_progress() {
    if (progress_listener_ == nullptr) {
        return;
    }

    progress_listener_->set_title(fs::path(gltf_file_).filename().string());
    progress_listener_->set_phase("Loading scene");
}

bool GltfAsyncLoader::load_gltf_file() {
    begin_gltf_progress();

    if (!ensure_gltf_parsed()) {
        if (progress_listener_ != nullptr) {
            progress_listener_->fail(gltf_parse_error_.empty() ? "Failed to load glTF file" : gltf_parse_error_);
        }
        OC_WARNING_FORMAT(
            "Failed to load glTF file: {}, Error: {}",
            gltf_file_.c_str(),
            gltf_parse_error_.c_str());
        return false;
    }

    tinygltf::Model& gltf_model = *gltf_model_;

    if (gltf_model.scenes.empty()) {
        if (progress_listener_ != nullptr) {
            progress_listener_->fail("glTF file has no scenes");
        }
        OC_WARNING_FORMAT("glTF file has no scenes: {}", gltf_file_.c_str());
        return false;
    }

    const float4x4 identity = Transform<float4x4>().mat4x4();
    const tinygltf::Scene& scene = gltf_model.scenes[gltf_model.defaultScene >= 0 ? gltf_model.defaultScene : 0];
    for (int node_index : scene.nodes) {
        if (node_index < 0 || node_index >= static_cast<int>(gltf_model.nodes.size())) {
            continue;
        }
        load_gltf_node(gltf_model.nodes[node_index], gltf_model, identity);
    }

    if (progress_listener_ != nullptr) {
        progress_listener_->set_phase("Building scene clusters");
    }

    build_scene_clusters();

    if (progress_listener_ != nullptr) {
        progress_listener_->complete();
    }
    return true;
}

void GltfAsyncLoader::build_scene_clusters() {
    scene_.build_grid();
}

void GltfAsyncLoader::load_gltf_node(
    const tinygltf::Node& node,
    const tinygltf::Model& model,
    const float4x4& parent_transform) {
    const float4x4 world_transform = parent_transform * node_local_transform(node);

    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (size_t gltf_primitive_index = 0; gltf_primitive_index < mesh.primitives.size(); ++gltf_primitive_index) {
            const tinygltf::Primitive& gltf_primitive = mesh.primitives[gltf_primitive_index];

            // CPU mesh first (GPU upload may still be in flight).
            Mesh* mesh_obj = get_or_create_mesh(gltf_primitive, model);
            if (mesh_obj == nullptr) {
                if (progress_listener_ != nullptr) {
                    progress_listener_->advance();
                }
                continue;
            }

            // CPU material with params + texture handles (GPU bindless flush may still be pending).
            Material* material = nullptr;
            if (gltf_primitive.material >= 0
                && gltf_primitive.material < static_cast<int>(model.materials.size())) {
                material = create_material(model.materials[gltf_primitive.material], model);
            } else {
                material = create_default_material();
            }

            // Enter the scene only after CPU mesh + material are fully prepared.
            const uint32_t scene_entity_index = scene_.emplace_renderable();

            float3 translation;
            quaternion rotation;
            float3 scale;
            decompose(world_transform, &translation, &rotation, &scale);
            scene_.transform_component(scene_entity_index).set_position(translation);
            scene_.transform_component(scene_entity_index).set_rotation(rotation);
            scene_.transform_component(scene_entity_index).set_scale(scale);

            RenderComponent& render = scene_.render_component(scene_entity_index);
            render.set_mesh(mesh_obj);
            if (material != nullptr) {
                render.set_material(material);
            }

            if (progress_listener_ != nullptr) {
                progress_listener_->advance();
            }
        }
    }

    for (int child_index : node.children) {
        if (child_index < 0 || child_index >= static_cast<int>(model.nodes.size())) {
            continue;
        }
        load_gltf_node(model.nodes[child_index], model, world_transform);
    }
}

Mesh* GltfAsyncLoader::get_or_create_mesh(
    const tinygltf::Primitive& gltf_primitive,
    const tinygltf::Model& model) {
    const uint64_t geometry_key = make_geometry_key(gltf_primitive);
    const auto cached_mesh = geometry_meshes_.find(geometry_key);
    if (cached_mesh != geometry_meshes_.end()) {
        OC_INFO_FORMAT(
            "GltfAsyncLoader: reused mesh geometry key={:#x} (skipped vertex/index load)",
            geometry_key);
        return cached_mesh->second;
    }

    Mesh* mesh_obj = ocarina::new_with_allocator<Mesh>();
    mesh_storage_.push_back(mesh_obj);

    const BoundingBox local_bounds = append_primitive_geometry(gltf_primitive, model, mesh_obj);
    if (local_bounds.valid) {
        mesh_obj->set_local_bounds(local_bounds.min, local_bounds.max);
    }

    geometry_meshes_.emplace(geometry_key, mesh_obj);
    return mesh_obj;
}

BoundingBox GltfAsyncLoader::append_primitive_geometry(
    const tinygltf::Primitive& primitive,
    const tinygltf::Model& model,
    Mesh* mesh) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t geometry_key = make_geometry_key(primitive);
    BoundingBox local_bounds;

    MeshPositions positions = make_mesh_positions();
    MeshNormals normals = make_mesh_normals();
    MeshTangents tangents = make_mesh_tangents();
    MeshUvs uvs = make_mesh_uvs();
    MeshColors colors = make_mesh_colors();
    bool has_normals = false;
    bool has_tangents = false;
    bool has_uvs = false;
    bool has_colors = false;

    for (const auto& attr : primitive.attributes) {
        const std::string& name = attr.first;
        const int accessor_index = attr.second;
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
            continue;
        }

        const tinygltf::Accessor& accessor = model.accessors[accessor_index];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
            continue;
        }

        const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
        if (buffer_view.buffer < 0 || buffer_view.buffer >= static_cast<int>(model.buffers.size())) {
            continue;
        }

        const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];
        const unsigned char* data = buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;
        const int count = accessor.count;
        const int component_type = accessor.componentType;
        const int type = accessor.type;

        if (name == "POSITION") {
            if (type == TINYGLTF_TYPE_VEC3 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                positions.resize(static_cast<size_t>(count));
                memcpy(positions.data(), data, static_cast<size_t>(count) * sizeof(Vector3));
                local_bounds = bounds_from_position_accessor(accessor);
                if (!local_bounds.valid) {
                    for (int vertex_index = 0; vertex_index < count; ++vertex_index) {
                        const Vector3& position = positions[static_cast<size_t>(vertex_index)];
                        local_bounds.expand(make_float3(position.x, position.y, position.z));
                    }
                }
            }
        } else if (name == "NORMAL") {
            if (type == TINYGLTF_TYPE_VEC3 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                normals.resize(static_cast<size_t>(count));
                memcpy(normals.data(), data, static_cast<size_t>(count) * sizeof(Vector3));
                has_normals = true;
            }
        } else if (name == "TANGENT") {
            if (type == TINYGLTF_TYPE_VEC4 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                tangents.resize(static_cast<size_t>(count));
                memcpy(tangents.data(), data, static_cast<size_t>(count) * sizeof(Vector4));
                has_tangents = true;
            }
        } else if (name == "TEXCOORD_0") {
            if (type == TINYGLTF_TYPE_VEC2 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                uvs.resize(static_cast<size_t>(count));
                memcpy(uvs.data(), data, static_cast<size_t>(count) * sizeof(Vector2));
                has_uvs = true;
            }
        } else if (name == "COLOR_0") {
            if (type == TINYGLTF_TYPE_VEC4 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                colors.resize(static_cast<size_t>(count));
                memcpy(colors.data(), data, static_cast<size_t>(count) * sizeof(Vector4));
                has_colors = true;
            } else if (type == TINYGLTF_TYPE_VEC3 && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                colors.resize(static_cast<size_t>(count));
                const auto* src = reinterpret_cast<const Vector3*>(data);
                for (int i = 0; i < count; ++i) {
                    colors[static_cast<size_t>(i)] = Vector4(src[i].x, src[i].y, src[i].z, 1.0f);
                }
                has_colors = true;
            }
        }
    }

    const uint32_t vertex_count = position_vertex_count(positions);
    OC_ASSERT(vertex_count > 0);

    if (!has_normals) {
        normals.clear();
    }
    if (!has_tangents) {
        tangents.clear();
    }
    if (!has_uvs) {
        uvs.clear();
    }
    if (!has_colors) {
        colors.clear();
    }

    MeshIndices indices = make_mesh_indices();
    if (primitive.indices >= 0 && primitive.indices < static_cast<int>(model.accessors.size())) {
        const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
        if (accessor.bufferView >= 0 && accessor.bufferView < static_cast<int>(model.bufferViews.size())) {
            const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
            if (buffer_view.buffer >= 0 && buffer_view.buffer < static_cast<int>(model.buffers.size())) {
                const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];
                const unsigned char* data = buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;
                const int count = accessor.count;
                indices.resize(static_cast<size_t>(count));
                if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    memcpy(indices.data(), data, static_cast<size_t>(count) * sizeof(uint16_t));
                } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    const auto* src = reinterpret_cast<const uint32_t*>(data);
                    for (int i = 0; i < count; ++i) {
                        indices[static_cast<size_t>(i)] = static_cast<uint16_t>(src[i]);
                    }
                }
            }
        }
    }

    if (indices.empty()) {
        indices.resize(vertex_count);
        for (uint32_t i = 0; i < vertex_count; ++i) {
            indices[i] = static_cast<uint16_t>(i);
        }
    }

    OwnedMeshGeometry geometry;
    geometry.positions = std::move(positions);
    if (has_normals) {
        geometry.normals = std::move(normals);
    }
    if (has_tangents) {
        geometry.tangents = std::move(tangents);
    }
    if (has_uvs) {
        geometry.uvs = std::move(uvs);
    }
    if (has_colors) {
        geometry.colors = std::move(colors);
    }
    geometry.indices = std::move(indices);
    GlobalGPUStorage::instance().upload_mesh(std::move(geometry), mesh);

    OC_INFO_FORMAT(
        "GltfAsyncLoader::append_primitive_geometry: geometry key={:#x}, {} vertices, {:.3f} ms",
        geometry_key,
        vertex_count,
        elapsed_ms(start));

    return local_bounds;
}

TextureHandle GltfAsyncLoader::load_gltf_image(int image_index, const tinygltf::Model& model, bool srgb) {
    if (image_index < 0 || image_index >= static_cast<int>(model.images.size())) {
        return TextureHandle{};
    }

    // Cache key must distinguish sRGB vs linear views of the same image file.
    const int cache_key = srgb ? image_index : -(image_index + 1);
    const auto cached = image_textures_.find(cache_key);
    if (cached != image_textures_.end()) {
        OC_INFO_FORMAT(
            "GltfAsyncLoader::load_gltf_image: cache hit image_index {} srgb={} (0.000 ms)",
            image_index,
            srgb);
        return cached->second;
    }

    if (progress_listener_ != nullptr) {
        progress_listener_->set_phase("Loading textures");
    }

    const auto start = std::chrono::steady_clock::now();

    const tinygltf::Image& gltf_image = model.images[image_index];
    fs::path image_path = gltf_directory_;
    if (!gltf_image.uri.empty()) {
        image_path /= gltf_image.uri;
    } else {
        image_path /= ("embedded_image_" + std::to_string(image_index));
    }

    GltfPixelSource pixels = resolve_gltf_image_pixels(gltf_image, gltf_directory_);
    if (pixels.data == nullptr || pixels.width == 0 || pixels.height == 0) {
        OC_WARNING_FORMAT("Failed to resolve pixel data for glTF image index {}", image_index);
        return TextureHandle{};
    }

    TextureViewCreation texture_view{};
    texture_view.mip_level_count = 0;
    texture_view.usage = TextureUsageFlags::ShaderReadOnly;
    texture_view.srgb = srgb;
    TextureSampler sampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};

    const std::string texture_name =
        image_path.filename().string() + (srgb ? "_srgb" : "_linear");
    TextureHandle handle = ResourceManager::instance().get_texture_handle(
        texture_name, texture_view, sampler);
    if (handle.bindless_index_ == InvalidUI32) {
        handle = ResourceManager::instance().create_texture(
            device_,
            texture_name,
            pixels.width,
            pixels.height,
            pixels.format,
            texture_view,
            sampler,
            pixels.data);
    }

    image_textures_.emplace(cache_key, handle);
    OC_INFO_FORMAT(
        "GltfAsyncLoader::load_gltf_image: image_index {} ({}), srgb={}, bindless={}, {:.3f} ms",
        image_index,
        texture_name.c_str(),
        srgb,
        handle.bindless_index_,
        elapsed_ms(start));
    return handle;
}

Material* GltfAsyncLoader::create_default_material() {
    if (shader_program_ == nullptr) {
        return nullptr;
    }

    Material* prim_material = ResourceManager::instance().create_unique_material(
        device_,
        shader_program_);
    if (prim_material == nullptr) {
        return nullptr;
    }

    prim_material->set_property("baseColorFactor", make_float4(1.f, 1.f, 1.f, 1.f));
    prim_material->set_property("roughness", 1.f);
    prim_material->set_property("metallic", 0.f);
    prim_material->set_property("ao", 1.f);
    prim_material->set_property("normalIndex", InvalidUI32);
    prim_material->set_property("normalSamplerIndex", 0u);
    prim_material->set_property("metallicRoughnessIndex", InvalidUI32);
    prim_material->set_property("metallicRoughnessSamplerIndex", 0u);
    return prim_material;
}

Material* GltfAsyncLoader::create_material(
    const tinygltf::Material& material,
    const tinygltf::Model& model) {
    const bool alpha_blend = material.alphaMode == "BLEND";
    ShaderProgram* program = (alpha_blend && alpha_blend_shader_program_ != nullptr)
        ? alpha_blend_shader_program_
        : shader_program_;
    if (program == nullptr) {
        return nullptr;
    }

    Material* prim_material = ResourceManager::instance().create_unique_material(
        device_,
        program);
    if (prim_material == nullptr) {
        return nullptr;
    }

    const auto& pbr = material.pbrMetallicRoughness;
    // baseColorFactor.a is the glTF opacity scalar (defaults to 1 when omitted).
    const float4 base_color_factor = make_float4(
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2]),
        static_cast<float>(pbr.baseColorFactor[3]));
    // glTF defaults: metallicFactor=1, roughnessFactor=1. Real values usually live in
    // metallicRoughnessTexture (G/B); sample that or non-metal materials shade as metal.
    const float roughness = static_cast<float>(pbr.roughnessFactor);
    const float metallic = static_cast<float>(pbr.metallicFactor);
    const float ao = 1.f;

    prim_material->set_property("baseColorFactor", base_color_factor);
    prim_material->set_property("roughness", roughness);
    prim_material->set_property("metallic", metallic);
    prim_material->set_property("ao", ao);
    prim_material->set_property("normalIndex", InvalidUI32);
    prim_material->set_property("normalSamplerIndex", 0u);
    prim_material->set_property("metallicRoughnessIndex", InvalidUI32);
    prim_material->set_property("metallicRoughnessSamplerIndex", 0u);

    const uint32_t linear_repeat_sampler = get_bindless_sampler_index(
        TextureSampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT});

    if (pbr.baseColorTexture.index >= 0 &&
        pbr.baseColorTexture.index < static_cast<int>(model.textures.size())) {
        const int image_index = model.textures[pbr.baseColorTexture.index].source;
        const TextureHandle albedo_handle = load_gltf_image(image_index, model, true);
        if (albedo_handle.bindless_index_ != InvalidUI32) {
            prim_material->set_bindless_texture("albedoIndex", albedo_handle);
            prim_material->set_property(
                "albedoIndex",
                &albedo_handle.bindless_index_,
                sizeof(albedo_handle.bindless_index_));
            prim_material->set_property("albedoSamplerIndex", linear_repeat_sampler);
        }
    }

    if (pbr.metallicRoughnessTexture.index >= 0 &&
        pbr.metallicRoughnessTexture.index < static_cast<int>(model.textures.size())) {
        const int image_index = model.textures[pbr.metallicRoughnessTexture.index].source;
        const TextureHandle mr_handle = load_gltf_image(image_index, model, false);
        if (mr_handle.bindless_index_ != InvalidUI32) {
            prim_material->set_bindless_texture("metallicRoughnessIndex", mr_handle);
            prim_material->set_property(
                "metallicRoughnessIndex",
                &mr_handle.bindless_index_,
                sizeof(mr_handle.bindless_index_));
            prim_material->set_property("metallicRoughnessSamplerIndex", linear_repeat_sampler);
        }
    }

    if (material.normalTexture.index >= 0 &&
        material.normalTexture.index < static_cast<int>(model.textures.size())) {
        const int image_index = model.textures[material.normalTexture.index].source;
        const TextureHandle normal_handle = load_gltf_image(image_index, model, false);
        if (normal_handle.bindless_index_ != InvalidUI32) {
            prim_material->set_bindless_texture("normalIndex", normal_handle);
            prim_material->set_property(
                "normalIndex",
                &normal_handle.bindless_index_,
                sizeof(normal_handle.bindless_index_));
            prim_material->set_property("normalSamplerIndex", linear_repeat_sampler);
        }
    }

    // glTF alphaMode "BLEND" → ALPHA_BLEND shader + alpha-blend pipeline state.
    if (alpha_blend) {
        prim_material->set_alpha_blend_enabled(true);
    }

    return prim_material;
}

}// namespace ocarina
