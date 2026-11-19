#pragma once

#include "voxel_types.h"
#include "framework/bounding_box.h"
#include "framework/global_gpu_storage.h"
#include "core/stl.h"

namespace ocarina {

/// CPU mesh for one material batch inside a chunk. A batch is split only when
/// the index buffer would exceed 16-bit indices. Each batch becomes one entity.
struct VoxelMergedBatch {
    VoxelMaterialId material = kVoxelAir;
    /// Solid voxels of this material in the chunk, including ones hidden inside the merge.
    uint32_t voxel_count = 0;
    uint32_t quad_count = 0;
    BoundingBox local_bounds{};
    OwnedMeshGeometry geometry;
};

/// Greedy face mesher.
/// Faces shared by the same material are omitted. Remaining coplanar faces of
/// one material are merged into rectangles. `sample` is in chunk-local coordinates
/// and may fall outside [0, chunk_size) so neighboring chunks can hide a border face.
/// `sample_color` supplies per-voxel RGB used as vertex color on emitted quads.
OC_VOXEL_API void build_voxel_greedy_quads(
    uint32_t chunk_size,
    const ocarina::function<VoxelMaterialId(int32_t x, int32_t y, int32_t z)>& sample,
    const ocarina::function<float3(int32_t x, int32_t y, int32_t z)>& sample_color,
    ocarina_vector<VoxelGreedyQuad>& quads);

OC_VOXEL_API void build_voxel_merged_batches(
    const ocarina_vector<VoxelGreedyQuad>& quads,
    const ocarina_unordered_map<VoxelMaterialId, uint32_t>& voxel_counts,
    ocarina_vector<VoxelMergedBatch>& batches);

}// namespace ocarina
