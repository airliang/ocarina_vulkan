#include "gltf_async_loader.h"
#include "enki_task_debug.h"
#include "loading_progress_listener.h"
#include "renderer.h"

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
#include "material_asset.h"
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
    // glTF 2.0: matrix overrides TRS when present (column-major 4x4).
    if (node.matrix.size() == 16) {
        float4x4 matrix{};
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                matrix[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
            }
        }
        return matrix;
    }

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

[[nodiscard]] const unsigned char* accessor_data_ptr(
    const tinygltf::Accessor& accessor,
    const tinygltf::Model& model) noexcept {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }
    const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
    if (buffer_view.buffer < 0 || buffer_view.buffer >= static_cast<int>(model.buffers.size())) {
        return nullptr;
    }
    const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];
    return buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;
}

/// Copy tightly-packed or strided float attribute elements into a contiguous destination.
template<typename T>
bool copy_float_accessor(
    const tinygltf::Accessor& accessor,
    const tinygltf::Model& model,
    int expected_type,
    ocarina_vector<T>& out) {
    if (accessor.type != expected_type
        || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return false;
    }
    const unsigned char* data = accessor_data_ptr(accessor, model);
    if (data == nullptr || accessor.count <= 0) {
        return false;
    }
    const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
    const int byte_stride = accessor.ByteStride(buffer_view);
    if (byte_stride <= 0) {
        return false;
    }

    constexpr size_t element_bytes = sizeof(T);
    out.resize(static_cast<size_t>(accessor.count));
    if (static_cast<size_t>(byte_stride) == element_bytes) {
        memcpy(out.data(), data, static_cast<size_t>(accessor.count) * element_bytes);
    } else {
        for (int i = 0; i < accessor.count; ++i) {
            memcpy(
                &out[static_cast<size_t>(i)],
                data + static_cast<size_t>(i) * static_cast<size_t>(byte_stride),
                element_bytes);
        }
    }
    return true;
}

bool copy_index_accessor(
    const tinygltf::Accessor& accessor,
    const tinygltf::Model& model,
    MeshIndices& out) {
    if (accessor.type != TINYGLTF_TYPE_SCALAR || accessor.count <= 0) {
        return false;
    }
    const unsigned char* data = accessor_data_ptr(accessor, model);
    if (data == nullptr) {
        return false;
    }
    const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
    const int byte_stride = accessor.ByteStride(buffer_view);
    if (byte_stride <= 0) {
        return false;
    }

    out.resize(static_cast<size_t>(accessor.count));
    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        for (int i = 0; i < accessor.count; ++i) {
            out[static_cast<size_t>(i)] = data[static_cast<size_t>(i) * static_cast<size_t>(byte_stride)];
        }
        return true;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        if (byte_stride == static_cast<int>(sizeof(uint16_t))) {
            memcpy(out.data(), data, static_cast<size_t>(accessor.count) * sizeof(uint16_t));
        } else {
            for (int i = 0; i < accessor.count; ++i) {
                uint16_t value = 0;
                memcpy(
                    &value,
                    data + static_cast<size_t>(i) * static_cast<size_t>(byte_stride),
                    sizeof(uint16_t));
                out[static_cast<size_t>(i)] = value;
            }
        }
        return true;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        for (int i = 0; i < accessor.count; ++i) {
            uint32_t value = 0;
            memcpy(
                &value,
                data + static_cast<size_t>(i) * static_cast<size_t>(byte_stride),
                sizeof(uint32_t));
            if (value > 0xffffu) {
                OC_ERROR_FORMAT(
                    "GltfAsyncLoader: index {} exceeds uint16 range ({})",
                    i,
                    value);
                out.clear();
                return false;
            }
            out[static_cast<size_t>(i)] = static_cast<uint16_t>(value);
        }
        return true;
    default:
        OC_ERROR_FORMAT(
            "GltfAsyncLoader: unsupported index componentType {}",
            accessor.componentType);
        return false;
    }
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
    Renderer* renderer,
    ocarina_string vertex_shader,
    ocarina_string pixel_shader,
    const ocarina_string& gltf_file)
    : AsyncLoader(scheduler, device, nullptr)
    , gltf_file_(gltf_file)
    , gltf_directory_(fs::path(gltf_file).parent_path())
    , renderer_(renderer)
    , vertex_shader_(std::move(vertex_shader))
    , pixel_shader_(std::move(pixel_shader)) {
}

GltfAsyncLoader::~GltfAsyncLoader() noexcept {
    for (Mesh* mesh : mesh_storage_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    mesh_storage_.clear();
    for (Material* material : material_storage_) {
        ResourceManager::instance().release_unique_material(material);
    }
    material_storage_.clear();
}

void GltfAsyncLoader::reset_for_reload(const ocarina_string& gltf_file) {
    scene_.clear_entities();

    for (Mesh* mesh : mesh_storage_) {
        ocarina::delete_with_allocator<Mesh>(mesh);
    }
    mesh_storage_.clear();
    geometry_meshes_.clear();

    for (Material* material : material_storage_) {
        ResourceManager::instance().release_unique_material(material);
    }
    material_storage_.clear();
    image_textures_.clear();

    gltf_model_.reset();
    gltf_model_parsed_ = false;
    gltf_parse_success_ = false;
    gltf_parse_error_.clear();
    is_loaded_ = false;

    gltf_file_ = gltf_file;
    gltf_directory_ = fs::path(gltf_file_).parent_path();
}

void GltfAsyncLoader::load(Device* device) {
    if (is_loaded_) {
        return;
    }

    device_ = device;
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
    std::string parse_error;
    std::string warning;
    const std::string gltf_file_path(gltf_file_.data(), gltf_file_.size());
    const bool binary = fs::path(gltf_file_).extension() == ".glb";
    gltf_parse_success_ = binary
        ? gltf_context.LoadBinaryFromFile(gltf_model_.get(), &parse_error, &warning, gltf_file_path)
        : gltf_context.LoadASCIIFromFile(gltf_model_.get(), &parse_error, &warning, gltf_file_path);

    gltf_parse_error_ = ocarina::to_ocarina_string(parse_error);
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

    // Always begin (or restart) so glTF-only reloads show the loader window after reset().
    const uint32_t steps = count_load_progress_steps();
    progress_listener_->begin(
        ocarina::to_ocarina_string(fs::path(gltf_file_).filename().string()),
        steps);
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
            // mesh.vert reads g_transforms[transform_index]; entity_index is the ECS slot.
            render.set_update_push_constant_function(
                [](RenderComponent& rc, uint32_t entity_index) {
                    rc.set_push_constant_variable("transform_index", entity_index);
                });

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
        const ocarina_string name = ocarina::to_ocarina_string(attr.first);
        const int accessor_index = attr.second;
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
            continue;
        }

        const tinygltf::Accessor& accessor = model.accessors[accessor_index];

        if (name == "POSITION") {
            if (copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC3, positions)) {
                local_bounds = bounds_from_position_accessor(accessor);
                if (!local_bounds.valid) {
                    for (const Vector3& position : positions) {
                        local_bounds.expand(make_float3(position.x, position.y, position.z));
                    }
                }
            }
        } else if (name == "NORMAL") {
            has_normals = copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC3, normals);
        } else if (name == "TANGENT") {
            has_tangents = copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC4, tangents);
        } else if (name == "TEXCOORD_0") {
            has_uvs = copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC2, uvs);
        } else if (name == "COLOR_0") {
            if (copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC4, colors)) {
                has_colors = true;
            } else if (accessor.type == TINYGLTF_TYPE_VEC3
                       && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                MeshPositions rgb = make_mesh_positions();
                if (copy_float_accessor(accessor, model, TINYGLTF_TYPE_VEC3, rgb)) {
                    colors.resize(rgb.size());
                    for (size_t i = 0; i < rgb.size(); ++i) {
                        colors[i] = Vector4(rgb[i].x, rgb[i].y, rgb[i].z, 1.0f);
                    }
                    has_colors = true;
                }
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

    // glTF 2.0 indices may be UNSIGNED_BYTE / UNSIGNED_SHORT / UNSIGNED_INT.
    // AlphaBlendModeTest uses UNSIGNED_BYTE; leaving them unfilled collapses draws to index 0.
    MeshIndices indices = make_mesh_indices();
    if (primitive.indices >= 0 && primitive.indices < static_cast<int>(model.accessors.size())) {
        copy_index_accessor(model.accessors[primitive.indices], model, indices);
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

    const ocarina_string texture_name = ocarina::to_ocarina_string(
        image_path.filename().string() + (srgb ? "_srgb" : "_linear"));
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

namespace {

enum class GltfAlphaMode : uint8_t {
    Opaque = 0,
    Mask,
    Blend,
};

[[nodiscard]] GltfAlphaMode gltf_alpha_mode(const tinygltf::Material* material) noexcept {
    if (material == nullptr) {
        return GltfAlphaMode::Opaque;
    }
    if (material->alphaMode == "BLEND") {
        return GltfAlphaMode::Blend;
    }
    if (material->alphaMode == "MASK") {
        return GltfAlphaMode::Mask;
    }
    return GltfAlphaMode::Opaque;
}

void apply_cull_override(MaterialTechniqueOverrides& overrides, bool double_sided) noexcept {
    overrides.has_cull_mode = true;
    overrides.cull_mode = double_sided ? CullingMode::NONE : CullingMode::BACK;
}

}// namespace

MaterialAsset GltfAsyncLoader::convert_gltf_material_to_asset(
        const tinygltf::Material* material,
        bool alpha_blend) const {
    const GltfAlphaMode alpha_mode = alpha_blend
        ? GltfAlphaMode::Blend
        : gltf_alpha_mode(material);
    const bool is_blend = alpha_mode == GltfAlphaMode::Blend;
    const bool is_mask = alpha_mode == GltfAlphaMode::Mask;
    const bool double_sided = material != nullptr && material->doubleSided;

    MaterialAsset asset;
    asset.version = 1;
    asset.name = (material != nullptr && !material->name.empty())
        ? ocarina::to_ocarina_string(material->name)
        : ocarina_string(is_blend ? "gltf_transparent" : (is_mask ? "gltf_masked" : "gltf_opaque"));
    asset.shading_model = is_blend ? "Transparent" : "Opaque";
    asset.template_name = "StandardPBR";

    // Adapt depth policy to the active pipeline: Equal/no-write only when a DepthPrepass exists.
    const bool has_depth_prepass = renderer_ != nullptr
        && renderer_->find_pipeline_pass("DepthPrepass") != nullptr;

    MaterialTechniqueDesc technique;
    technique.name = is_blend ? "ForwardTransparent" : "ForwardOpaque";
    technique.vertex_shader = vertex_shader_;
    technique.pixel_shader = pixel_shader_;
    // MASK → ALPHA_MASK=1; BLEND → ALPHA_BLEND=1 (+ MaterialBlendMode::Alpha).
    technique.macros.push_back(make_shader_option("ALPHA_BLEND", is_blend ? 1 : 0));
    technique.macros.push_back(make_shader_option("ALPHA_MASK", is_mask ? 1 : 0));
    technique.overrides.has_blend = true;
    technique.overrides.blend = is_blend ? MaterialBlendMode::Alpha : MaterialBlendMode::Opaque;
    technique.overrides.has_depth_write = true;
    technique.overrides.has_depth_compare = true;
    apply_cull_override(technique.overrides, double_sided);
    if (is_blend) {
        technique.overrides.depth_write = false;
        technique.overrides.depth_compare = SamplerCompareFunc::LE;
    } else if (has_depth_prepass) {
        // After DepthPrepass: color pass tests Equal against prepass depth.
        technique.overrides.depth_write = false;
        technique.overrides.depth_compare = SamplerCompareFunc::E;
    } else {
        // Opaque-only pipeline: this pass owns depth.
        technique.overrides.depth_write = true;
        technique.overrides.depth_compare = SamplerCompareFunc::LE;
    }
    asset.techniques.push_back(std::move(technique));

    if (!is_blend) {
        asset.pass_participation["Opaque"] = "ForwardOpaque";
        if (has_depth_prepass) {
            const fs::path depth_frag_path =
                fs::path(vertex_shader_.c_str()).parent_path() / "mesh_depth.frag";
            MaterialTechniqueDesc depth_only;
            depth_only.name = "DepthOnly";
            depth_only.vertex_shader = vertex_shader_;
            depth_only.pixel_shader =
                ocarina::to_ocarina_string(fs::absolute(depth_frag_path).string());
            depth_only.overrides.has_color_write = true;
            depth_only.overrides.color_write = false;
            depth_only.overrides.has_depth_write = true;
            depth_only.overrides.depth_write = true;
            depth_only.overrides.has_depth_compare = true;
            depth_only.overrides.depth_compare = SamplerCompareFunc::LE;
            apply_cull_override(depth_only.overrides, double_sided);
            depth_only.overrides.has_blend = true;
            depth_only.overrides.blend = MaterialBlendMode::Opaque;
            asset.techniques.push_back(std::move(depth_only));
            asset.pass_participation["DepthPrepass"] = "DepthOnly";
        }
    } else {
        asset.pass_participation["Transparent"] = "ForwardTransparent";
    }

    // Bind RHIRenderPass from the already-loaded pipeline on Renderer.
    if (renderer_ != nullptr) {
        asset.bind_render_passes(*renderer_);
    }

    auto add_float4 = [&](const char* name, const float4& value) {
        MaterialPropertyDesc prop;
        prop.name = name;
        prop.type = MaterialPropertyValueType::Float4;
        prop.float4_value = value;
        prop.has_value = true;
        asset.properties.push_back(std::move(prop));
    };
    auto add_float = [&](const char* name, float value) {
        MaterialPropertyDesc prop;
        prop.name = name;
        prop.type = MaterialPropertyValueType::Float;
        prop.float_value = value;
        prop.has_value = true;
        asset.properties.push_back(std::move(prop));
    };

    if (material != nullptr) {
        const auto& pbr = material->pbrMetallicRoughness;
        add_float4(
            "baseColorFactor",
            make_float4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3])));
        add_float("roughness", static_cast<float>(pbr.roughnessFactor));
        add_float("metallic", static_cast<float>(pbr.metallicFactor));
        add_float("alphaCutoff", static_cast<float>(material->alphaCutoff));
    } else {
        add_float4("baseColorFactor", make_float4(1.f, 1.f, 1.f, 1.f));
        add_float("roughness", 1.f);
        add_float("metallic", 0.f);
        add_float("alphaCutoff", 0.5f);
    }
    add_float("ao", 1.f);

    return asset;
}

Material* GltfAsyncLoader::create_default_material() {
    MaterialAsset asset = convert_gltf_material_to_asset(nullptr, false);
    Material* prim_material = ResourceManager::instance().create_material_from_asset(
        device_,
        asset,
        progress_listener_);
    if (prim_material == nullptr) {
        return nullptr;
    }
    material_storage_.push_back(prim_material);

    prim_material->set_property("albedoIndex", InvalidUI32);
    prim_material->set_property("albedoSamplerIndex", 0u);
    prim_material->set_property("normalIndex", InvalidUI32);
    prim_material->set_property("normalSamplerIndex", 0u);
    prim_material->set_property("metallicRoughnessIndex", InvalidUI32);
    prim_material->set_property("metallicRoughnessSamplerIndex", 0u);
    return prim_material;
}

Material* GltfAsyncLoader::create_material(
    const tinygltf::Material& material,
    const tinygltf::Model& model) {
    // convert_gltf_material_to_asset reads alphaMode (OPAQUE / MASK / BLEND) from @p material.
    MaterialAsset asset = convert_gltf_material_to_asset(&material, false);
    Material* prim_material = ResourceManager::instance().create_material_from_asset(
        device_,
        asset,
        progress_listener_);
    if (prim_material == nullptr) {
        return nullptr;
    }
    material_storage_.push_back(prim_material);

    prim_material->set_property("albedoIndex", InvalidUI32);
    prim_material->set_property("albedoSamplerIndex", 0u);
    prim_material->set_property("normalIndex", InvalidUI32);
    prim_material->set_property("normalSamplerIndex", 0u);
    prim_material->set_property("metallicRoughnessIndex", InvalidUI32);
    prim_material->set_property("metallicRoughnessSamplerIndex", 0u);

    const auto& pbr = material.pbrMetallicRoughness;
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

    return prim_material;
}

}// namespace ocarina
