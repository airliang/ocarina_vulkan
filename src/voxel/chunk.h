#pragma once

#include "voxel_types.h"
#include "voxel_world_data.h"
#include "framework/bounding_box.h"
#include "framework/global_gpu_storage.h"
#include "framework/mesh_cpu_memory.h"
#include "core/stl.h"

namespace ocarina {

/// One draw batch for a chunk's exposed-face mesh. Geometry is filled from Chunk CPU buffers.
struct VoxelMergedBatch {
    uint32_t voxel_count = 0;
    uint32_t quad_count = 0;
    BoundingBox local_bounds{};
    OwnedMeshGeometry geometry;
};

/// Sparse chunk: solid voxels only. `position` is the chunk grid coordinate.
/// World AABB is `position * chunk_size` .. `(position + 1) * chunk_size` in cell units
/// (multiply by VoxelWorld::voxel_size_meters() for meters).
struct Chunk {
    uint32_t id = kInvalidChunkId;
    int3 position{};
    /// Sparse solids (edits / colored voxels). Used when dense_occupancy is empty.
    ocarina_vector<Voxel> voxels;
    /// Dense occupancy (0 = air, 1 = solid). Size chunk_size^3 when present.
    ocarina_vector<uint8_t> dense_occupancy;
    /// Used by color_at for dense solids with no sparse override.
    float3 default_color = make_float3(1.0f, 1.0f, 1.0f);

    /// CPU face mesh for this chunk (exposed faces only). Uploaded to mega VB/IB.
    MeshPositions positions = make_mesh_positions();
    MeshNormals normals = make_mesh_normals();
    MeshColors colors = make_mesh_colors();
    MeshIndices indices = make_mesh_indices();

    BoundingBox bounds{};
    ocarina_vector<MergedVoxelRender> merged_renders;
    bool mesh_dirty = true;

    [[nodiscard]] bool is_solid(uint32_t chunk_size, uint3 local) const noexcept;
    [[nodiscard]] float3 color_at(uint32_t chunk_size, uint3 local) const noexcept;
    [[nodiscard]] uint32_t solid_count(uint32_t chunk_size) const noexcept;

    /// True if the cell across @p face is solid within this chunk's occupancy.
    /// Out-of-chunk neighbors are treated as empty here; VoxelWorld supplies cross-chunk checks.
    [[nodiscard]] bool has_neighbor(
        uint32_t chunk_size,
        uint3 local,
        VoxelFace face) const noexcept;

    /// Appends exposed unit-cube faces for @p local into positions/normals/colors/indices.
    void append_voxel_faces(
        uint32_t chunk_size,
        uint3 local,
        const float3& color,
        const ocarina::function<bool(int32_t, int32_t, int32_t)>& sample_solid);

    void clear_cpu_mesh() noexcept;

    /// Rebuilds CPU positions/normals/colors/indices from exposed unit-cube faces.
    /// @p sample_solid is in chunk-local coordinates and may fall outside [0, chunk_size).
    void rebuild_face_mesh(
        uint32_t chunk_size,
        const ocarina::function<bool(int32_t x, int32_t y, int32_t z)>& sample_solid);

    /// Copies CPU mesh into a single upload batch (mega VB/IB).
    void build_mesh_batch(VoxelMergedBatch& out_batch) const;

    /// Inserts or updates a solid voxel (`solid == true`) or clears air (`solid == false`).
    /// When an existing solid cell is hit again, colors are averaged.
    /// Does not touch the CPU face mesh — marks mesh_dirty; call rebuild_face_mesh once.
    bool set_voxel(
        uint32_t chunk_size,
        uint3 local,
        bool solid,
        const float3& color = make_float3(1.0f));
};

}// namespace ocarina
