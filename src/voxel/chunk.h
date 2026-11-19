#pragma once

#include "voxel_types.h"
#include "framework/bounding_box.h"
#include "core/stl.h"

namespace ocarina {

/// Sparse chunk: solid voxels only. `position` is the chunk grid coordinate.
/// World AABB is `position * chunk_size` .. `(position + 1) * chunk_size` in cell units
/// (multiply by VoxelWorld::voxel_size_meters() for meters).
struct Chunk {
    uint32_t id = kInvalidChunkId;
    int3 position{};
    ocarina_vector<Voxel> voxels;
    BoundingBox bounds{};
    ocarina_vector<MergedVoxelRender> merged_renders;
    bool mesh_dirty = true;

    [[nodiscard]] VoxelMaterialId material_at(uint32_t chunk_size, uint3 local) const noexcept;
    [[nodiscard]] float3 color_at(uint32_t chunk_size, uint3 local) const noexcept;
    /// Inserts or updates a solid voxel. Air removes the cell. Returns true if data changed.
    /// When an existing solid cell is hit again, colors are averaged.
    bool set_voxel(
        uint32_t chunk_size,
        uint3 local,
        VoxelMaterialId material,
        const float3& color = make_float3(1.0f));
};

}// namespace ocarina
