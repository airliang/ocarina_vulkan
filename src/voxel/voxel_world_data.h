#pragma once

#include "voxel_types.h"
#include "rhi/vertex_buffer.h"
#include "core/stl.h"

namespace ocarina {

inline constexpr uint32_t kVoxelWorldDataChunkSize = kDefaultVoxelChunkSize;
inline constexpr uint32_t kVoxelWorldDataChunkVolume =
    kVoxelWorldDataChunkSize * kVoxelWorldDataChunkSize * kVoxelWorldDataChunkSize;

enum class VoxelFace : uint8_t {
    Bottom = 0,
    Front = 1,
    Right = 2,
    Top = 3,
    Left = 4,
    Back = 5,
    Count = 6
};

inline constexpr uint32_t kVoxelFaceCount = static_cast<uint32_t>(VoxelFace::Count);
inline constexpr uint32_t kVoxelFaceCornerCount = 4;
inline constexpr uint32_t kVoxelFaceIndexCount = 6;

/// Unit-cube face corners in local cell space [0, 1], plus face normal and vertex colors.
/// Not a full cube mesh — each face is emitted independently when exposed.
struct VoxelFaceVertices {
    Vector3 positions[kVoxelFaceCornerCount]{};
    Vector3 normal{};
    Vector4 colors[kVoxelFaceCornerCount]{};
};

struct VoxelCubeVertices {
    VoxelFaceVertices faces[kVoxelFaceCount]{};
};

/// Triangle list for one face quad (two tris), relative to the face's 4 corners.
struct VoxelFaceIndices {
    uint16_t indices[kVoxelFaceIndexCount]{0, 1, 2, 0, 2, 3};
};

[[nodiscard]] inline const VoxelCubeVertices& voxel_unit_cube_vertices() noexcept {
    static const VoxelCubeVertices kCube = [] {
        VoxelCubeVertices cube{};
        const Vector4 white{1.0f, 1.0f, 1.0f, 1.0f};

        auto fill_face = [&](VoxelFace face, Vector3 n, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3) {
            VoxelFaceVertices& out = cube.faces[static_cast<uint32_t>(face)];
            out.normal = n;
            out.positions[0] = p0;
            out.positions[1] = p1;
            out.positions[2] = p2;
            out.positions[3] = p3;
            out.colors[0] = white;
            out.colors[1] = white;
            out.colors[2] = white;
            out.colors[3] = white;
        };

        // Winding is counter-clockwise when viewed from outside (LH, +Y up).
        fill_face(
            VoxelFace::Bottom,
            Vector3{0.0f, -1.0f, 0.0f},
            Vector3{0.0f, 0.0f, 0.0f},
            Vector3{1.0f, 0.0f, 0.0f},
            Vector3{1.0f, 0.0f, 1.0f},
            Vector3{0.0f, 0.0f, 1.0f});
        fill_face(
            VoxelFace::Front,
            Vector3{0.0f, 0.0f, 1.0f},
            Vector3{0.0f, 0.0f, 1.0f},
            Vector3{1.0f, 0.0f, 1.0f},
            Vector3{1.0f, 1.0f, 1.0f},
            Vector3{0.0f, 1.0f, 1.0f});
        fill_face(
            VoxelFace::Right,
            Vector3{1.0f, 0.0f, 0.0f},
            Vector3{1.0f, 0.0f, 1.0f},
            Vector3{1.0f, 0.0f, 0.0f},
            Vector3{1.0f, 1.0f, 0.0f},
            Vector3{1.0f, 1.0f, 1.0f});
        fill_face(
            VoxelFace::Top,
            Vector3{0.0f, 1.0f, 0.0f},
            Vector3{0.0f, 1.0f, 1.0f},
            Vector3{1.0f, 1.0f, 1.0f},
            Vector3{1.0f, 1.0f, 0.0f},
            Vector3{0.0f, 1.0f, 0.0f});
        fill_face(
            VoxelFace::Left,
            Vector3{-1.0f, 0.0f, 0.0f},
            Vector3{0.0f, 0.0f, 0.0f},
            Vector3{0.0f, 0.0f, 1.0f},
            Vector3{0.0f, 1.0f, 1.0f},
            Vector3{0.0f, 1.0f, 0.0f});
        fill_face(
            VoxelFace::Back,
            Vector3{0.0f, 0.0f, -1.0f},
            Vector3{1.0f, 0.0f, 0.0f},
            Vector3{0.0f, 0.0f, 0.0f},
            Vector3{0.0f, 1.0f, 0.0f},
            Vector3{1.0f, 1.0f, 0.0f});
        return cube;
    }();
    return kCube;
}

[[nodiscard]] inline const VoxelFaceIndices& voxel_face_indices() noexcept {
    static const VoxelFaceIndices kIndices{};
    return kIndices;
}

[[nodiscard]] inline int3 voxel_face_neighbor_offset(VoxelFace face) noexcept {
    switch (face) {
        case VoxelFace::Bottom:
            return make_int3(0, -1, 0);
        case VoxelFace::Front:
            return make_int3(0, 0, 1);
        case VoxelFace::Right:
            return make_int3(1, 0, 0);
        case VoxelFace::Top:
            return make_int3(0, 1, 0);
        case VoxelFace::Left:
            return make_int3(-1, 0, 0);
        case VoxelFace::Back:
            return make_int3(0, 0, -1);
        default:
            return make_int3(0, 0, 0);
    }
}

/// True when the adjacent cell across @p face is solid (non-air).
template<typename SampleSolidFn>
[[nodiscard]] bool has_neighbor(
    VoxelFace face,
    int32_t local_x,
    int32_t local_y,
    int32_t local_z,
    SampleSolidFn&& sample_solid) noexcept {
    const int3 offset = voxel_face_neighbor_offset(face);
    return sample_solid(local_x + offset.x, local_y + offset.y, local_z + offset.z);
}

/// One chunk of dense occupancy for serialization.
/// Layout: voxels[x + y * N + z * N * N], N = chunk_size
/// 0 = empty, 1 = solid
struct ChunkData {
    int3 position{};
    uint8_t voxels[kVoxelWorldDataChunkVolume]{};
};

/// Serializable snapshot of a VoxelWorld (occupancy only).
struct VoxelWorldData {
    uint32_t chunk_size = kVoxelWorldDataChunkSize;
    float voxel_size_meters = 1.0f;
    ocarina_vector<ChunkData> chunks;
};

/// CPU 3D density texture (one float per cell). Used to build VoxelWorldData.
struct NoiseTexture3D {
    uint3 size{};
    ocarina_vector<float> density;
};

[[nodiscard]] inline uint32_t chunk_data_local_index(
    uint32_t chunk_size,
    uint32_t x,
    uint32_t y,
    uint32_t z) noexcept {
    return x + y * chunk_size + z * chunk_size * chunk_size;
}

[[nodiscard]] OC_VOXEL_API const ChunkData* find_chunk_data(
    const VoxelWorldData& data,
    int3 chunk_position) noexcept;

[[nodiscard]] OC_VOXEL_API uint8_t sample_voxel_world_data(
    const VoxelWorldData& data,
    int32_t world_x,
    int32_t world_y,
    int32_t world_z) noexcept;

/// Build chunk occupancy from a dense density field.
/// Cell is solid when density[i] > @p solid_threshold.
OC_VOXEL_API VoxelWorldData build_voxel_world_data_from_density(
    const float* density,
    uint3 size,
    float voxel_size_meters,
    float solid_threshold = 0.0f);

OC_VOXEL_API VoxelWorldData build_voxel_world_data_from_noise_texture(
    const NoiseTexture3D& texture,
    float voxel_size_meters,
    float solid_threshold = 0.0f);

}// namespace ocarina
