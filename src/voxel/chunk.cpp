#include "chunk.h"

namespace ocarina {

bool Chunk::is_solid(uint32_t chunk_size, uint3 local) const noexcept {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return false;
    }
    const uint32_t packed = pack_voxel_local(chunk_size, local);
    if (!dense_occupancy.empty()) {
        if (packed >= dense_occupancy.size()) {
            return false;
        }
        return dense_occupancy[packed] != 0;
    }
    for (const Voxel& voxel : voxels) {
        if (voxel.position == packed) {
            return true;
        }
    }
    return false;
}

float3 Chunk::color_at(uint32_t chunk_size, uint3 local) const noexcept {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return make_float3(1.0f);
    }
    const uint32_t packed = pack_voxel_local(chunk_size, local);
    for (const Voxel& voxel : voxels) {
        if (voxel.position == packed) {
            return voxel.color;
        }
    }
    if (!dense_occupancy.empty() &&
        packed < dense_occupancy.size() &&
        dense_occupancy[packed] != 0) {
        return default_color;
    }
    return make_float3(1.0f);
}

uint32_t Chunk::solid_count(uint32_t chunk_size) const noexcept {
    if (!dense_occupancy.empty()) {
        uint32_t count = 0;
        for (uint8_t occupancy : dense_occupancy) {
            if (occupancy != 0) {
                ++count;
            }
        }
        return count;
    }
    (void)chunk_size;
    return static_cast<uint32_t>(voxels.size());
}

bool Chunk::has_neighbor(uint32_t chunk_size, uint3 local, VoxelFace face) const noexcept {
    return ocarina::has_neighbor(
        face,
        static_cast<int32_t>(local.x),
        static_cast<int32_t>(local.y),
        static_cast<int32_t>(local.z),
        [&](int32_t x, int32_t y, int32_t z) noexcept {
            if (x < 0 || y < 0 || z < 0 ||
                x >= static_cast<int32_t>(chunk_size) ||
                y >= static_cast<int32_t>(chunk_size) ||
                z >= static_cast<int32_t>(chunk_size)) {
                return false;
            }
            return is_solid(
                chunk_size,
                make_uint3(
                    static_cast<uint32_t>(x),
                    static_cast<uint32_t>(y),
                    static_cast<uint32_t>(z)));
        });
}

void Chunk::clear_cpu_mesh() noexcept {
    positions.clear();
    normals.clear();
    colors.clear();
    indices.clear();
}

void Chunk::append_voxel_faces(
    uint32_t chunk_size,
    uint3 local,
    const float3& color,
    const ocarina::function<bool(int32_t, int32_t, int32_t)>& sample_solid) {
    (void)chunk_size;
    const VoxelCubeVertices& cube = voxel_unit_cube_vertices();
    const VoxelFaceIndices& face_indices = voxel_face_indices();
    const Vector4 vertex_color{color.x, color.y, color.z, 1.0f};
    const Vector3 origin{
        static_cast<float>(local.x),
        static_cast<float>(local.y),
        static_cast<float>(local.z)};

    for (uint32_t face_i = 0; face_i < kVoxelFaceCount; ++face_i) {
        const auto face = static_cast<VoxelFace>(face_i);
        if (ocarina::has_neighbor(
                face,
                static_cast<int32_t>(local.x),
                static_cast<int32_t>(local.y),
                static_cast<int32_t>(local.z),
                sample_solid)) {
            continue;
        }

        const VoxelFaceVertices& face_verts = cube.faces[face_i];
        const auto base = static_cast<uint16_t>(positions.size());
        for (uint32_t corner = 0; corner < kVoxelFaceCornerCount; ++corner) {
            const Vector3& local_corner = face_verts.positions[corner];
            positions.push_back(Vector3{
                origin.x + local_corner.x,
                origin.y + local_corner.y,
                origin.z + local_corner.z});
            normals.push_back(face_verts.normal);
            colors.push_back(vertex_color);
        }
        for (uint32_t i = 0; i < kVoxelFaceIndexCount; ++i) {
            indices.push_back(static_cast<uint16_t>(base + face_indices.indices[i]));
        }
    }
}

bool Chunk::set_voxel(
    uint32_t chunk_size,
    uint3 local,
    bool solid,
    const float3& color) {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return false;
    }

    const uint32_t packed = pack_voxel_local(chunk_size, local);

    if (!dense_occupancy.empty()) {
        if (packed >= dense_occupancy.size()) {
            return false;
        }
        if (!solid) {
            if (dense_occupancy[packed] == 0) {
                return false;
            }
            dense_occupancy[packed] = 0;
            for (size_t index = 0; index < voxels.size(); ++index) {
                if (voxels[index].position == packed) {
                    voxels[index] = voxels.back();
                    voxels.pop_back();
                    break;
                }
            }
            mesh_dirty = true;
            return true;
        }

        dense_occupancy[packed] = 1;
        for (Voxel& voxel : voxels) {
            if (voxel.position != packed) {
                continue;
            }
            const uint32_t samples = std::max(voxel.color_samples, 1u);
            const float3 sum = voxel.color * static_cast<float>(samples) + color;
            const uint32_t next_samples = samples + 1u;
            voxel.color = sum / static_cast<float>(next_samples);
            voxel.color_samples = next_samples;
            mesh_dirty = true;
            return true;
        }
        Voxel voxel{};
        voxel.chunk_id = id;
        voxel.position = packed;
        voxel.color = color;
        voxel.color_samples = 1;
        voxels.push_back(voxel);
        mesh_dirty = true;
        return true;
    }

    for (size_t index = 0; index < voxels.size(); ++index) {
        if (voxels[index].position != packed) {
            continue;
        }
        if (!solid) {
            voxels[index] = voxels.back();
            voxels.pop_back();
            mesh_dirty = true;
            return true;
        }

        Voxel& voxel = voxels[index];
        const uint32_t samples = std::max(voxel.color_samples, 1u);
        const float3 sum = voxel.color * static_cast<float>(samples) + color;
        const uint32_t next_samples = samples + 1u;
        voxel.color = sum / static_cast<float>(next_samples);
        voxel.color_samples = next_samples;
        mesh_dirty = true;
        return true;
    }

    if (!solid) {
        return false;
    }

    Voxel voxel{};
    voxel.chunk_id = id;
    voxel.position = packed;
    voxel.color = color;
    voxel.color_samples = 1;
    voxels.push_back(voxel);
    mesh_dirty = true;
    return true;
}

void Chunk::rebuild_face_mesh(
    uint32_t chunk_size,
    const ocarina::function<bool(int32_t x, int32_t y, int32_t z)>& sample_solid) {
    clear_cpu_mesh();

    const auto emit_local = [&](uint3 local, const float3& color) {
        append_voxel_faces(chunk_size, local, color, sample_solid);
    };

    if (!dense_occupancy.empty()) {
        const uint32_t volume = chunk_size * chunk_size * chunk_size;
        for (uint32_t packed = 0; packed < volume && packed < dense_occupancy.size(); ++packed) {
            if (dense_occupancy[packed] == 0) {
                continue;
            }
            const uint3 local = unpack_voxel_local(chunk_size, packed);
            emit_local(local, color_at(chunk_size, local));
        }
        return;
    }

    for (const Voxel& voxel : voxels) {
        emit_local(unpack_voxel_local(chunk_size, voxel.position), voxel.color);
    }
}

void Chunk::build_mesh_batch(VoxelMergedBatch& out_batch) const {
    out_batch = {};
    out_batch.voxel_count = static_cast<uint32_t>(voxels.size());
    if (!dense_occupancy.empty()) {
        uint32_t solids = 0;
        for (uint8_t value : dense_occupancy) {
            solids += value != 0 ? 1u : 0u;
        }
        out_batch.voxel_count = solids;
    }
    out_batch.quad_count = static_cast<uint32_t>(indices.size() / kVoxelFaceIndexCount);

    if (positions.empty() || indices.empty()) {
        return;
    }

    out_batch.geometry.positions = make_mesh_positions();
    out_batch.geometry.normals = make_mesh_normals();
    out_batch.geometry.colors = make_mesh_colors();
    out_batch.geometry.indices = make_mesh_indices();
    out_batch.geometry.positions.assign(positions.begin(), positions.end());
    out_batch.geometry.normals.assign(normals.begin(), normals.end());
    out_batch.geometry.colors.assign(colors.begin(), colors.end());
    out_batch.geometry.indices.assign(indices.begin(), indices.end());

    for (const Vector3& position : positions) {
        out_batch.local_bounds.expand(make_float3(position.x, position.y, position.z));
    }
}

}// namespace ocarina
