#include "mesh.h"
#include "global_gpu_storage.h"
#include "resource_manager.h"
#include <cmath>

namespace ocarina {

Mesh::Mesh()
    : RHIResource(nullptr, Tag::MESH, 0) {
    mesh_id_ = ResourceManager::instance().register_mesh(this);
    set_gpu_resource_state(GPUResourceState::CPU_Loaded);
}

Mesh::~Mesh() {
    ResourceManager::instance().unregister_mesh(this);
}

namespace {

constexpr float kPi = 3.14159265358979323846f;

void append_vertex(
    std::vector<Vector3>& positions,
    std::vector<Vector3>& normals,
    std::vector<Vector2>& uvs,
    std::vector<Vector4>& colors,
    const Vector3& position,
    const Vector3& normal,
    const Vector2& uv) {
    positions.push_back(position);
    normals.push_back(normal);
    uvs.push_back(uv);
    colors.push_back(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
}

void add_face(
    std::vector<Vector3>& positions,
    std::vector<Vector3>& normals,
    std::vector<Vector2>& uvs,
    std::vector<Vector4>& colors,
    std::vector<uint16_t>& indices,
    const Vector3& v0,
    const Vector3& v1,
    const Vector3& v2,
    const Vector3& v3,
    const Vector3& normal) {
    const uint16_t base = static_cast<uint16_t>(positions.size());
    append_vertex(positions, normals, uvs, colors, v0, normal, Vector2(0.0f, 0.0f));
    append_vertex(positions, normals, uvs, colors, v1, normal, Vector2(1.0f, 0.0f));
    append_vertex(positions, normals, uvs, colors, v2, normal, Vector2(1.0f, 1.0f));
    append_vertex(positions, normals, uvs, colors, v3, normal, Vector2(0.0f, 1.0f));
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

}// namespace

void Mesh::set_local_bounds(const float3& min_point, const float3& max_point) noexcept {
    local_bounds_.min = min_point;
    local_bounds_.max = max_point;
    local_bounds_.valid = true;
}

Mesh* Mesh::create_quad() {
    Mesh* mesh = ResourceManager::instance().get_mesh("quad");
    if (!mesh) {
        mesh = ocarina::new_with_allocator<Quad>();
        ResourceManager::instance().add_mesh("quad", mesh);
    }
    return mesh;
}

Mesh* Mesh::create_cube() {
    Mesh* mesh = ResourceManager::instance().get_mesh("cube");
    if (!mesh) {
        mesh = ocarina::new_with_allocator<Cube>();
        ResourceManager::instance().add_mesh("cube", mesh);
    }
    return mesh;
}

Mesh* Mesh::create_sphere() {
    Mesh* mesh = ResourceManager::instance().get_mesh("sphere");
    if (!mesh) {
        mesh = ocarina::new_with_allocator<Sphere>();
        ResourceManager::instance().add_mesh("sphere", mesh);
    }
    return mesh;
}

Quad::Quad() {
    OwnedMeshGeometry geometry;
    geometry.positions = {
        {-1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f},
    };
    geometry.uvs = {
        {1.0f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 0.0f},
        {1.0f, 0.0f},
    };
    geometry.colors = {
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    };
    geometry.indices = {0, 1, 2, 2, 3, 0};
    GlobalGPUStorage::instance().upload_mesh(std::move(geometry), this);
}

Quad::~Quad() {
}

Cube::Cube() {
    constexpr float half_extent = 0.5f;

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector4> colors;
    std::vector<uint16_t> indices;
    positions.reserve(24);
    normals.reserve(24);
    uvs.reserve(24);
    colors.reserve(24);
    indices.reserve(36);

    add_face(positions, normals, uvs, colors, indices,
        Vector3(-half_extent, -half_extent, half_extent),
        Vector3(half_extent, -half_extent, half_extent),
        Vector3(half_extent, half_extent, half_extent),
        Vector3(-half_extent, half_extent, half_extent),
        Vector3(0.0f, 0.0f, 1.0f));

    add_face(positions, normals, uvs, colors, indices,
        Vector3(half_extent, -half_extent, -half_extent),
        Vector3(-half_extent, -half_extent, -half_extent),
        Vector3(-half_extent, half_extent, -half_extent),
        Vector3(half_extent, half_extent, -half_extent),
        Vector3(0.0f, 0.0f, -1.0f));

    add_face(positions, normals, uvs, colors, indices,
        Vector3(-half_extent, half_extent, half_extent),
        Vector3(half_extent, half_extent, half_extent),
        Vector3(half_extent, half_extent, -half_extent),
        Vector3(-half_extent, half_extent, -half_extent),
        Vector3(0.0f, 1.0f, 0.0f));

    add_face(positions, normals, uvs, colors, indices,
        Vector3(-half_extent, -half_extent, -half_extent),
        Vector3(half_extent, -half_extent, -half_extent),
        Vector3(half_extent, -half_extent, half_extent),
        Vector3(-half_extent, -half_extent, half_extent),
        Vector3(0.0f, -1.0f, 0.0f));

    add_face(positions, normals, uvs, colors, indices,
        Vector3(half_extent, -half_extent, half_extent),
        Vector3(half_extent, -half_extent, -half_extent),
        Vector3(half_extent, half_extent, -half_extent),
        Vector3(half_extent, half_extent, half_extent),
        Vector3(1.0f, 0.0f, 0.0f));

    add_face(positions, normals, uvs, colors, indices,
        Vector3(-half_extent, -half_extent, -half_extent),
        Vector3(-half_extent, -half_extent, half_extent),
        Vector3(-half_extent, half_extent, half_extent),
        Vector3(-half_extent, half_extent, -half_extent),
        Vector3(-1.0f, 0.0f, 0.0f));

    OwnedMeshGeometry geometry;
    geometry.positions = std::move(positions);
    geometry.normals = std::move(normals);
    geometry.uvs = std::move(uvs);
    geometry.colors = std::move(colors);
    geometry.indices = std::move(indices);
    GlobalGPUStorage::instance().upload_mesh(std::move(geometry), this);
    set_local_bounds(
        make_float3(-half_extent, -half_extent, -half_extent),
        make_float3(half_extent, half_extent, half_extent));
}

Cube::~Cube() {
}

Sphere::Sphere(uint32_t slice_count, uint32_t stack_count) {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector4> colors;
    std::vector<uint16_t> indices;

    for (uint32_t stack = 0; stack <= stack_count; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(stack_count);
        const float phi = v * kPi;
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (uint32_t slice = 0; slice <= slice_count; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slice_count);
            const float theta = u * 2.0f * kPi;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            const Vector3 normal = {
                sin_phi * cos_theta,
                cos_phi,
                sin_phi * sin_theta,
            };
            append_vertex(
                positions,
                normals,
                uvs,
                colors,
                normal,
                normal,
                Vector2(u, 1.0f - v));
        }
    }

    for (uint32_t stack = 0; stack < stack_count; ++stack) {
        for (uint32_t slice = 0; slice < slice_count; ++slice) {
            const uint16_t first = static_cast<uint16_t>(stack * (slice_count + 1) + slice);
            const uint16_t second = static_cast<uint16_t>(first + static_cast<uint16_t>(slice_count + 1));

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    OwnedMeshGeometry geometry;
    geometry.positions = std::move(positions);
    geometry.normals = std::move(normals);
    geometry.uvs = std::move(uvs);
    geometry.colors = std::move(colors);
    geometry.indices = std::move(indices);
    GlobalGPUStorage::instance().upload_mesh(std::move(geometry), this);
    set_local_bounds(make_float3(-1.0f, -1.0f, -1.0f), make_float3(1.0f, 1.0f, 1.0f));
}

Sphere::~Sphere() {
}

}// namespace ocarina
