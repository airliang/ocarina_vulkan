#include "mesh_voxelizer.h"
#include "framework/math.h"
#include "framework/transform.h"
#include "core/logging.h"

#include "ext/tinygltf/tiny_gltf.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ocarina {

namespace {

[[nodiscard]] float3 mul_point(const float4x4& m, const float3& p) noexcept {
    const float4 h = m * make_float4(p.x, p.y, p.z, 1.0f);
    const float inv_w = (std::abs(h.w) > 1.0e-8f) ? (1.0f / h.w) : 1.0f;
    return make_float3(h.x * inv_w, h.y * inv_w, h.z * inv_w);
}

[[nodiscard]] float4x4 node_local_transform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        float4x4 m{};
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                m[c][r] = static_cast<float>(node.matrix[c * 4 + r]);
            }
        }
        return m;
    }

    float3 t = make_float3(0.0f);
    if (node.translation.size() == 3) {
        t = make_float3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2]));
    }
    quaternion r{};
    if (node.rotation.size() == 4) {
        r = quaternion(
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
            static_cast<float>(node.rotation[3]));
    }
    float3 s = make_float3(1.0f);
    if (node.scale.size() == 3) {
        s = make_float3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2]));
    }
    Transform<float4x4> transform;
    transform.set_TRS(t, r, s);
    return transform.mat4x4();
}

[[nodiscard]] bool load_model(const fs::path& path, tinygltf::Model& model, ocarina_string& error) {
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    const std::string path_str = path.string();
    const bool ok = path.extension() == ".glb"
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path_str)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path_str);
    if (!warn.empty()) {
        OC_WARNING_FORMAT("MeshVoxelizer glTF warning: {}", warn.c_str());
    }
    if (!ok) {
        error = ocarina::to_ocarina_string(err.empty() ? "failed to load glTF" : err);
        return false;
    }
    return true;
}

template<typename T>
[[nodiscard]] bool read_accessor_floats(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    int expected_type,
    ocarina_vector<T>& out) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }
    if (accessor.type != expected_type || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const size_t stride = accessor.ByteStride(view);
    const size_t comps = sizeof(T) / sizeof(float);
    out.resize(static_cast<size_t>(accessor.count));
    const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    for (size_t i = 0; i < out.size(); ++i) {
        const float* src = reinterpret_cast<const float*>(data + i * stride);
        float* dst = reinterpret_cast<float*>(&out[i]);
        for (size_t c = 0; c < comps; ++c) {
            dst[c] = src[c];
        }
    }
    return true;
}

[[nodiscard]] bool read_indices(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    ocarina_vector<uint32_t>& out) {
    out.clear();
    if (primitive.indices < 0) {
        return false;
    }
    const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const size_t stride = accessor.ByteStride(view);
    const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    out.resize(static_cast<size_t>(accessor.count));
    for (size_t i = 0; i < out.size(); ++i) {
        const uint8_t* ptr = data + i * stride;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = *ptr;
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                memcpy(&value, ptr, sizeof(value));
                out[i] = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                memcpy(&value, ptr, sizeof(value));
                out[i] = value;
                break;
            }
            default:
                out.clear();
                return false;
        }
    }
    return true;
}

// Tomas Akenine-Möller triangle–AABB overlap (separating axis theorem).
[[nodiscard]] bool triangle_aabb_overlap(
    const float3& v0,
    const float3& v1,
    const float3& v2,
    const float3& box_center,
    const float3& box_half) noexcept {
    const float3 c0 = v0 - box_center;
    const float3 c1 = v1 - box_center;
    const float3 c2 = v2 - box_center;

    const float3 e0 = c1 - c0;
    const float3 e1 = c2 - c1;
    const float3 e2 = c0 - c2;

    const auto axis_test = [&](float a, float b, float fa, float fb,
                               float v0a, float v0b, float v1a, float v1b,
                               float ha, float hb) noexcept {
        const float p0 = a * v0a - b * v0b;
        const float p1 = a * v1a - b * v1b;
        const float min_p = std::min(p0, p1);
        const float max_p = std::max(p0, p1);
        const float rad = fa * ha + fb * hb;
        return min_p > rad || max_p < -rad;
    };

    const float3 fe0 = make_float3(std::abs(e0.x), std::abs(e0.y), std::abs(e0.z));
    const float3 fe1 = make_float3(std::abs(e1.x), std::abs(e1.y), std::abs(e1.z));
    const float3 fe2 = make_float3(std::abs(e2.x), std::abs(e2.y), std::abs(e2.z));

    if (axis_test(e0.z, e0.y, fe0.z, fe0.y, c0.y, c0.z, c2.y, c2.z, box_half.y, box_half.z)) return false;
    if (axis_test(e0.z, e0.x, fe0.z, fe0.x, c0.x, c0.z, c2.x, c2.z, box_half.x, box_half.z)) return false;
    if (axis_test(e0.y, e0.x, fe0.y, fe0.x, c1.x, c1.y, c2.x, c2.y, box_half.x, box_half.y)) return false;

    if (axis_test(e1.z, e1.y, fe1.z, fe1.y, c0.y, c0.z, c2.y, c2.z, box_half.y, box_half.z)) return false;
    if (axis_test(e1.z, e1.x, fe1.z, fe1.x, c0.x, c0.z, c2.x, c2.z, box_half.x, box_half.z)) return false;
    if (axis_test(e1.y, e1.x, fe1.y, fe1.x, c0.x, c0.y, c1.x, c1.y, box_half.x, box_half.y)) return false;

    if (axis_test(e2.z, e2.y, fe2.z, fe2.y, c0.y, c0.z, c1.y, c1.z, box_half.y, box_half.z)) return false;
    if (axis_test(e2.z, e2.x, fe2.z, fe2.x, c0.x, c0.z, c1.x, c1.z, box_half.x, box_half.z)) return false;
    if (axis_test(e2.y, e2.x, fe2.y, fe2.x, c1.x, c1.y, c2.x, c2.y, box_half.x, box_half.y)) return false;

    const float min_x = std::min({c0.x, c1.x, c2.x});
    const float max_x = std::max({c0.x, c1.x, c2.x});
    if (min_x > box_half.x || max_x < -box_half.x) return false;
    const float min_y = std::min({c0.y, c1.y, c2.y});
    const float max_y = std::max({c0.y, c1.y, c2.y});
    if (min_y > box_half.y || max_y < -box_half.y) return false;
    const float min_z = std::min({c0.z, c1.z, c2.z});
    const float max_z = std::max({c0.z, c1.z, c2.z});
    if (min_z > box_half.z || max_z < -box_half.z) return false;

    const float3 normal = cross(e0, e1);
    const float d = -dot(normal, c0);
    const float3 vmin{
        normal.x > 0.0f ? -box_half.x : box_half.x,
        normal.y > 0.0f ? -box_half.y : box_half.y,
        normal.z > 0.0f ? -box_half.z : box_half.z};
    if (dot(normal, vmin) + d > 0.0f) return false;
    const float3 vmax = -vmin;
    if (dot(normal, vmax) + d < 0.0f) return false;
    return true;
}

[[nodiscard]] float3 barycentric(const float3& p, const float3& a, const float3& b, const float3& c) noexcept {
    const float3 v0 = b - a;
    const float3 v1 = c - a;
    const float3 v2 = p - a;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1.0e-12f) {
        return make_float3(1.0f, 0.0f, 0.0f);
    }
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    const float u = 1.0f - v - w;
    return make_float3(u, v, w);
}

[[nodiscard]] float3 closest_point_on_triangle(
    const float3& p,
    const float3& a,
    const float3& b,
    const float3& c) noexcept {
    const float3 ab = b - a;
    const float3 ac = c - a;
    const float3 ap = p - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }
    const float3 bp = p - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return a + ab * v;
    }
    const float3 cp = p - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return a + ab * v + ac * w;
}

struct AlbedoSampler {
    float4 factor = make_float4(1.0f);
    const uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int components = 4;

    [[nodiscard]] float3 sample(float2 uv) const noexcept {
        float3 color = make_float3(factor.x, factor.y, factor.z);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return color;
        }
        float u = uv.x - std::floor(uv.x);
        float v = uv.y - std::floor(uv.y);
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(width)), 0, width - 1);
        const int y = std::clamp(static_cast<int>((1.0f - v) * static_cast<float>(height)), 0, height - 1);
        const uint8_t* px = pixels + (static_cast<size_t>(y) * width + x) * components;
        const float inv = 1.0f / 255.0f;
        float3 tex = make_float3(px[0] * inv, px[1] * inv, px[2] * inv);
        // Approximate sRGB → linear for lighting-friendly vertex colors.
        tex = make_float3(std::pow(tex.x, 2.2f), std::pow(tex.y, 2.2f), std::pow(tex.z, 2.2f));
        return tex * color;
    }
};

[[nodiscard]] AlbedoSampler make_albedo_sampler(
    const tinygltf::Model& model,
    int material_index) {
    AlbedoSampler sampler;
    if (material_index < 0 || material_index >= static_cast<int>(model.materials.size())) {
        return sampler;
    }
    const tinygltf::Material& material = model.materials[material_index];
    const auto& pbr = material.pbrMetallicRoughness;
    if (pbr.baseColorFactor.size() >= 3) {
        sampler.factor = make_float4(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            pbr.baseColorFactor.size() >= 4 ? static_cast<float>(pbr.baseColorFactor[3]) : 1.0f);
    }
    if (pbr.baseColorTexture.index < 0 ||
        pbr.baseColorTexture.index >= static_cast<int>(model.textures.size())) {
        return sampler;
    }
    const int image_index = model.textures[pbr.baseColorTexture.index].source;
    if (image_index < 0 || image_index >= static_cast<int>(model.images.size())) {
        return sampler;
    }
    const tinygltf::Image& image = model.images[image_index];
    if (image.image.empty() || image.width <= 0 || image.height <= 0) {
        return sampler;
    }
    sampler.pixels = image.image.data();
    sampler.width = image.width;
    sampler.height = image.height;
    sampler.components = std::max(image.component, 1);
    return sampler;
}

void expand_bounds(BoundingBox& bounds, const float3& p) noexcept {
    bounds.expand(p);
}

void gather_bounds_node(
    const tinygltf::Model& model,
    int node_index,
    const float4x4& parent,
    BoundingBox& bounds) {
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) {
        return;
    }
    const tinygltf::Node& node = model.nodes[node_index];
    const float4x4 local = parent * node_local_transform(node);
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            const auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end()) {
                continue;
            }
            ocarina_vector<float3> positions;
            if (!read_accessor_floats(model, model.accessors[pos_it->second], TINYGLTF_TYPE_VEC3, positions)) {
                continue;
            }
            for (const float3& p : positions) {
                expand_bounds(bounds, mul_point(local, p));
            }
        }
    }
    for (int child : node.children) {
        gather_bounds_node(model, child, local, bounds);
    }
}

void voxelize_primitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    const float4x4& world,
    float voxel_size,
    VoxelMaterialId solid_material,
    VoxelWorld& out_world,
    uint32_t& triangle_count) {
    const auto pos_it = primitive.attributes.find("POSITION");
    if (pos_it == primitive.attributes.end()) {
        return;
    }

    ocarina_vector<float3> positions;
    ocarina_vector<float2> uvs;
    if (!read_accessor_floats(model, model.accessors[pos_it->second], TINYGLTF_TYPE_VEC3, positions)) {
        return;
    }
    const auto uv_it = primitive.attributes.find("TEXCOORD_0");
    if (uv_it != primitive.attributes.end()) {
        (void)read_accessor_floats(model, model.accessors[uv_it->second], TINYGLTF_TYPE_VEC2, uvs);
    }

    ocarina_vector<uint32_t> indices;
    if (!read_indices(model, primitive, indices)) {
        indices.resize(positions.size());
        for (uint32_t i = 0; i < indices.size(); ++i) {
            indices[i] = i;
        }
    }
    if (indices.size() < 3) {
        return;
    }

    const AlbedoSampler albedo = make_albedo_sampler(model, primitive.material);
    const float half = voxel_size * 0.5f;
    const float3 box_half = make_float3(half, half, half);

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
            continue;
        }

        const float3 w0 = mul_point(world, positions[i0]);
        const float3 w1 = mul_point(world, positions[i1]);
        const float3 w2 = mul_point(world, positions[i2]);
        ++triangle_count;

        const float3 tri_min = make_float3(
            std::min({w0.x, w1.x, w2.x}),
            std::min({w0.y, w1.y, w2.y}),
            std::min({w0.z, w1.z, w2.z}));
        const float3 tri_max = make_float3(
            std::max({w0.x, w1.x, w2.x}),
            std::max({w0.y, w1.y, w2.y}),
            std::max({w0.z, w1.z, w2.z}));

        const int min_x = static_cast<int>(std::floor(tri_min.x / voxel_size));
        const int min_y = static_cast<int>(std::floor(tri_min.y / voxel_size));
        const int min_z = static_cast<int>(std::floor(tri_min.z / voxel_size));
        const int max_x = static_cast<int>(std::floor(tri_max.x / voxel_size));
        const int max_y = static_cast<int>(std::floor(tri_max.y / voxel_size));
        const int max_z = static_cast<int>(std::floor(tri_max.z / voxel_size));

        const float2 uv0 = (i0 < uvs.size()) ? uvs[i0] : make_float2(0.0f, 0.0f);
        const float2 uv1 = (i1 < uvs.size()) ? uvs[i1] : make_float2(0.0f, 0.0f);
        const float2 uv2 = (i2 < uvs.size()) ? uvs[i2] : make_float2(0.0f, 0.0f);

        for (int z = min_z; z <= max_z; ++z) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const float3 center = make_float3(
                        (static_cast<float>(x) + 0.5f) * voxel_size,
                        (static_cast<float>(y) + 0.5f) * voxel_size,
                        (static_cast<float>(z) + 0.5f) * voxel_size);
                    if (!triangle_aabb_overlap(w0, w1, w2, center, box_half)) {
                        continue;
                    }

                    const float3 on_tri = closest_point_on_triangle(center, w0, w1, w2);
                    const float3 bary = barycentric(on_tri, w0, w1, w2);
                    const float2 uv = make_float2(
                        bary.x * uv0.x + bary.y * uv1.x + bary.z * uv2.x,
                        bary.x * uv0.y + bary.y * uv1.y + bary.z * uv2.y);
                    const float3 color = albedo.sample(uv);
                    out_world.set_voxel_world(center, solid_material, color);
                }
            }
        }
    }
}

void voxelize_node(
    const tinygltf::Model& model,
    int node_index,
    const float4x4& parent,
    float voxel_size,
    VoxelMaterialId solid_material,
    VoxelWorld& out_world,
    uint32_t& triangle_count) {
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) {
        return;
    }
    const tinygltf::Node& node = model.nodes[node_index];
    const float4x4 local = parent * node_local_transform(node);
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        for (const tinygltf::Primitive& primitive : model.meshes[node.mesh].primitives) {
            voxelize_primitive(
                model, primitive, local, voxel_size, solid_material, out_world, triangle_count);
        }
    }
    for (int child : node.children) {
        voxelize_node(model, child, local, voxel_size, solid_material, out_world, triangle_count);
    }
}

}// namespace

BoundingBox MeshVoxelizer::compute_gltf_bounds(const fs::path& gltf_path) {
    tinygltf::Model model;
    ocarina_string error;
    BoundingBox bounds;
    if (!load_model(gltf_path, model, error)) {
        OC_ERROR_FORMAT("MeshVoxelizer::compute_gltf_bounds: {}", error.c_str());
        return bounds;
    }

    float4x4 identity(1.0f);
    if (!model.scenes.empty()) {
        const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (scene_index < static_cast<int>(model.scenes.size())) {
            for (int node : model.scenes[scene_index].nodes) {
                gather_bounds_node(model, node, identity, bounds);
            }
        }
    } else if (!model.nodes.empty()) {
        gather_bounds_node(model, 0, identity, bounds);
    }
    return bounds;
}

float MeshVoxelizer::suggest_voxel_size(const BoundingBox& bounds) noexcept {
    if (!bounds.valid) {
        return 0.1f;
    }
    const float ex = bounds.max.x - bounds.min.x;
    const float ey = bounds.max.y - bounds.min.y;
    const float ez = bounds.max.z - bounds.min.z;
    const float min_extent = std::max(1.0e-4f, std::min({ex, ey, ez}));
    return min_extent / 10.0f;
}

MeshVoxelizerResult MeshVoxelizer::voxelize_gltf(
    const fs::path& gltf_path,
    float voxel_size_meters,
    VoxelWorld& world,
    VoxelMaterialId solid_material) {
    MeshVoxelizerResult result;
    result.suggested_voxel_size = voxel_size_meters;

    tinygltf::Model model;
    if (!load_model(gltf_path, model, result.error)) {
        return result;
    }

    float4x4 identity(1.0f);
    result.mesh_bounds_meters = {};
    if (!model.scenes.empty()) {
        const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (scene_index < static_cast<int>(model.scenes.size())) {
            for (int node : model.scenes[scene_index].nodes) {
                gather_bounds_node(model, node, identity, result.mesh_bounds_meters);
            }
        }
    } else if (!model.nodes.empty()) {
        gather_bounds_node(model, 0, identity, result.mesh_bounds_meters);
    }
    if (!result.mesh_bounds_meters.valid) {
        result.error = ocarina::to_ocarina_string("mesh has empty bounds");
        return result;
    }

    const float size = std::max(voxel_size_meters, 1.0e-5f);
    world.clear();
    world.set_voxel_size_meters(size);

    if (!model.scenes.empty()) {
        const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (scene_index < static_cast<int>(model.scenes.size())) {
            for (int node : model.scenes[scene_index].nodes) {
                voxelize_node(
                    model, node, identity, size, solid_material, world, result.triangle_count);
            }
        }
    } else if (!model.nodes.empty()) {
        voxelize_node(
            model, 0, identity, size, solid_material, world, result.triangle_count);
    }

    result.solid_voxel_count = world.solid_voxel_count();
    result.ok = result.solid_voxel_count > 0;
    if (!result.ok) {
        result.error = ocarina::to_ocarina_string("voxelization produced no solid voxels");
    }
    return result;
}

}// namespace ocarina
