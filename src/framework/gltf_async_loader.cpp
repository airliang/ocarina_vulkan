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
#include "primitive.h"
#include "mesh.h"
#include "material.h"
#include "scene.h"
#include "global_gpu_storage.h"
#include "bounding_box.h"
#include "resource_manager.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
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

uint32_t position_vertex_count(const std::vector<Vector3>& positions) {
    return static_cast<uint32_t>(positions.size());
}

struct GltfPixelSource {
    PixelStorage format = PixelStorage::BYTE4;
    const void* data = nullptr;
    std::vector<uint8_t> owned;
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
        Image image = Image::load(gltf_directory / gltf_image.uri, ColorSpace::SRGB);
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
    const std::string& gltf_file,
    Device* device,
    Material* shared_material,
    LoadingProgressListener* progress_listener)
    : gltf_file_(gltf_file)
    , gltf_directory_(fs::path(gltf_file).parent_path())
    , device_(device)
    , shared_material_(shared_material)
    , progress_listener_(progress_listener) {}

GltfAsyncLoader::~GltfAsyncLoader() noexcept {
    for (Mesh* mesh : mesh_storage_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    mesh_storage_.clear();
}

void GltfAsyncLoader::Execute() {
    if (!is_loaded_) {
        is_loaded_ = load_gltf_file();
    }
}

bool GltfAsyncLoader::load_gltf_file() {
    if (progress_listener_ != nullptr) {
        progress_listener_->begin(fs::path(gltf_file_).filename().string());
        progress_listener_->set_phase("Parsing glTF");
    }

    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF gltf_context;
    std::string error;
    std::string warning;

    const bool binary = fs::path(gltf_file_).extension() == ".glb";
    const bool file_loaded = binary
        ? gltf_context.LoadBinaryFromFile(&gltf_model, &error, &warning, gltf_file_)
        : gltf_context.LoadASCIIFromFile(&gltf_model, &error, &warning, gltf_file_);

    if (!file_loaded) {
        if (progress_listener_ != nullptr) {
            progress_listener_->fail(error.empty() ? "Failed to load glTF file" : error);
        }
        OC_WARNING_FORMAT(
            "Failed to load glTF file: {}, Error: {}, Warning: {}",
            gltf_file_.c_str(),
            error.c_str(),
            warning.c_str());
        return false;
    }

    if (!warning.empty()) {
        OC_WARNING_FORMAT("glTF loader warning for {}: {}", gltf_file_.c_str(), warning.c_str());
    }

    if (gltf_model.scenes.empty()) {
        if (progress_listener_ != nullptr) {
            progress_listener_->fail("glTF file has no scenes");
        }
        OC_WARNING_FORMAT("glTF file has no scenes: {}", gltf_file_.c_str());
        return false;
    }

    if (progress_listener_ != nullptr) {
        const uint32_t primitive_count = count_gltf_primitives(gltf_model);
        const uint32_t image_count = static_cast<uint32_t>(gltf_model.images.size());
        const uint32_t total_steps = 1 + primitive_count + image_count + 1;
        progress_listener_->set_total_steps(total_steps);
        progress_listener_->advance();
        progress_listener_->set_phase("Loading scene");
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
        progress_listener_->advance();
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

            Primitive& prim = scene_.emplace_primitive();

            float3 translation;
            quaternion rotation;
            float3 scale;
            decompose(world_transform, &translation, &rotation, &scale);
            const uint32_t scene_primitive_index = static_cast<uint32_t>(scene_.primitive_count() - 1);
            scene_.transform_component(scene_primitive_index).set_position(translation);
            scene_.transform_component(scene_primitive_index).set_rotation(rotation);
            scene_.transform_component(scene_primitive_index).set_scale(scale);

            Mesh* mesh_obj = nullptr;
            const uint64_t geometry_key = make_geometry_key(gltf_primitive);
            const auto cached_mesh = geometry_meshes_.find(geometry_key);
            if (cached_mesh != geometry_meshes_.end()) {
                mesh_obj = cached_mesh->second;
                OC_INFO_FORMAT(
                    "GltfAsyncLoader: reused mesh geometry key={:#x} (skipped vertex/index load)",
                    geometry_key);
            } else {
                mesh_obj = ocarina::new_with_allocator<Mesh>();
                mesh_storage_.push_back(mesh_obj);

                MeshGeometrySlice geometry_slice{};
                const BoundingBox local_bounds = append_primitive_geometry(gltf_primitive, model, geometry_slice);
                mesh_obj->set_geometry_slice(geometry_slice);
                if (local_bounds.valid) {
                    mesh_obj->set_local_bounds(local_bounds.min, local_bounds.max);
                }

                geometry_meshes_.emplace(geometry_key, mesh_obj);
            }

            prim.set_mesh(mesh_obj);

            if (shared_material_ != nullptr) {
                prim.set_material(shared_material_);
            }

            if (gltf_primitive.material >= 0 && gltf_primitive.material < static_cast<int>(model.materials.size())) {
                load_material(prim, model.materials[gltf_primitive.material], model);
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

BoundingBox GltfAsyncLoader::append_primitive_geometry(
    const tinygltf::Primitive& primitive,
    const tinygltf::Model& model,
    MeshGeometrySlice& out_slice) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t geometry_key = make_geometry_key(primitive);
    BoundingBox local_bounds;

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector4> colors;
    bool has_normals = false;
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
    if (!has_uvs) {
        uvs.clear();
    }
    if (!has_colors) {
        colors.clear();
    }

    std::vector<uint16_t> indices;
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

    MeshGeometryInput input{};
    input.vertex_count = vertex_count;
    input.positions = positions.data();
    input.normals = has_normals ? normals.data() : nullptr;
    input.uvs = has_uvs ? uvs.data() : nullptr;
    input.colors = has_colors ? colors.data() : nullptr;
    input.indices = indices.data();
    input.index_count = static_cast<uint32_t>(indices.size());
    out_slice = GlobalGPUStorage::instance().append_mesh(input);

    OC_INFO_FORMAT(
        "GltfAsyncLoader::append_primitive_geometry: geometry key={:#x}, {} vertices, {:.3f} ms",
        geometry_key,
        vertex_count,
        elapsed_ms(start));

    return local_bounds;
}

Texture* GltfAsyncLoader::load_gltf_image(int image_index, const tinygltf::Model& model) {
    if (image_index < 0 || image_index >= static_cast<int>(model.images.size())) {
        return nullptr;
    }

    const auto cached = image_textures_.find(image_index);
    if (cached != image_textures_.end()) {
        OC_INFO_FORMAT(
            "GltfAsyncLoader::load_gltf_image: cache hit image_index {} (0.000 ms)",
            image_index);
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
        return nullptr;
    }

    TextureViewCreation texture_view{};
    texture_view.mip_level_count = 0;
    texture_view.usage = TextureUsageFlags::ShaderReadOnly;
    TextureSampler sampler{TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};

    const std::string texture_name = image_path.filename().string();
    Texture* texture = ResourceManager::instance().get_texture(texture_name, texture_view, sampler);
    if (texture == nullptr) {
        texture = ResourceManager::instance().create_texture(
            device_,
            texture_name,
            pixels.width,
            pixels.height,
            pixels.format,
            texture_view,
            sampler,
            pixels.data);
    }

    image_textures_.emplace(image_index, texture);
    if (progress_listener_ != nullptr) {
        progress_listener_->advance();
    }
    OC_INFO_FORMAT(
        "GltfAsyncLoader::load_gltf_image: image_index {} ({}), {:.3f} ms",
        image_index,
        texture_name.c_str(),
        elapsed_ms(start));
    return texture;
}

void GltfAsyncLoader::load_material(Primitive& prim, const tinygltf::Material& material, const tinygltf::Model& model) {
    if (shared_material_ != nullptr) {
        prim.set_material(shared_material_);
    }

    if (material.pbrMetallicRoughness.baseColorTexture.index < 0) {
        return;
    }

    const int texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
    if (texture_index < 0 || texture_index >= static_cast<int>(model.textures.size())) {
        return;
    }

    const int image_index = model.textures[texture_index].source;
    Texture* texture = load_gltf_image(image_index, model);
    if (texture == nullptr) {
        return;
    }

    prim.add_bindless_texture(hash64("albedo"), texture);
    prim.add_sampler(hash64("sampler_albedo"), *texture->get_sampler_pointer());
}

}// namespace ocarina
