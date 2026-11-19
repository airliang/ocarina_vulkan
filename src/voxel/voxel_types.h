#pragma once

#include "core/header.h"
#include "math/basic_types.h"
#include "math/constants.h"

#if defined(_MSC_VER)
#if defined(OC_VOXEL_EXPORT_DLL)
#define OC_VOXEL_API __declspec(dllexport)
#else
#define OC_VOXEL_API __declspec(dllimport)
#endif
#else
#define OC_VOXEL_API
#endif

namespace ocarina {

class Mesh;

inline constexpr uint32_t kDefaultVoxelChunkSize = 32;
inline constexpr uint32_t kMaxVoxelChunkSize = 128;

using VoxelMaterialId = uint16_t;
inline constexpr VoxelMaterialId kVoxelAir = 0;

/// 0 is never a live chunk. Ids are assigned by VoxelWorld and are not reused.
inline constexpr uint32_t kInvalidChunkId = 0;

/// Packed local cell: x + y * chunk_size + z * chunk_size^2.
/// Local cells run from (0,0,0) to (chunk_size - 1) on each axis.
[[nodiscard]] inline uint32_t pack_voxel_local(uint32_t chunk_size, uint3 local) noexcept {
    return local.x + local.y * chunk_size + local.z * chunk_size * chunk_size;
}

[[nodiscard]] inline uint3 unpack_voxel_local(uint32_t chunk_size, uint32_t packed) noexcept {
    const uint32_t area = chunk_size * chunk_size;
    const uint32_t z = packed / area;
    const uint32_t rem = packed - z * area;
    const uint32_t y = rem / chunk_size;
    const uint32_t x = rem - y * chunk_size;
    return make_uint3(x, y, z);
}

[[nodiscard]] inline bool is_valid_voxel_local(uint32_t chunk_size, uint3 local) noexcept {
    return local.x < chunk_size && local.y < chunk_size && local.z < chunk_size;
}

[[nodiscard]] inline bool is_valid_packed_voxel_local(uint32_t chunk_size, uint32_t packed) noexcept {
    return packed < chunk_size * chunk_size * chunk_size;
}

/// A voxel stores its chunk id, packed local cell, material, and surface color.
/// World position is not stored; it is recovered from chunk id + packed local cell.
struct Voxel {
    uint32_t chunk_id = kInvalidChunkId;
    uint32_t position = 0;
    VoxelMaterialId material = kVoxelAir;
    float3 color = make_float3(1.0f, 1.0f, 1.0f);
    uint32_t color_samples = 0;
};

struct ChunkCoordHash {
    [[nodiscard]] size_t operator()(int3 coord) const noexcept {
        const auto mix = [](int32_t value) noexcept {
            return static_cast<uint32_t>(value);
        };
        return static_cast<size_t>(
            mix(coord.x) * 73856093u ^ mix(coord.y) * 19349663u ^ mix(coord.z) * 83492791u);
    }
};

struct ChunkCoordEqual {
    [[nodiscard]] bool operator()(int3 lhs, int3 rhs) const noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
};

/// One exposed rectangle produced by greedy meshing.
/// axis 0: u = Y, v = Z. axis 1: u = X, v = Z. axis 2: u = X, v = Y.
struct VoxelGreedyQuad {
    uint8_t axis = 0;
    int8_t sign = 1;
    uint32_t u = 0;
    uint32_t v = 0;
    uint32_t slice = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VoxelMaterialId material = kVoxelAir;
    float3 color = make_float3(1.0f, 1.0f, 1.0f);
};

/// One draw of a greedy-merged group. The ECS RenderComponent on `entity_index`
/// references `mesh` — a merged surface of voxels in `chunk_id`, not a single voxel.
struct MergedVoxelRender {
    uint32_t entity_index = InvalidUI32;
    uint32_t chunk_id = kInvalidChunkId;
    VoxelMaterialId material_id = kVoxelAir;
    uint32_t voxel_count = 0;
    uint32_t quad_count = 0;
    Mesh* mesh = nullptr;
};

}// namespace ocarina
