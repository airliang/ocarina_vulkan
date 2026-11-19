#pragma once

#include "voxel_world.h"
#include "framework/bounding_box.h"
#include "core/stl.h"

namespace ocarina {

struct MeshVoxelizerResult {
    BoundingBox mesh_bounds_meters{};
    float suggested_voxel_size = 0.1f;
    uint32_t triangle_count = 0;
    uint32_t solid_voxel_count = 0;
    bool ok = false;
    ocarina_string error;
};

/// CPU surface voxelization of a glTF mesh into @p world.
///
/// Algorithm: for each triangle, iterate voxels overlapping the triangle AABB and
/// keep cells that intersect the triangle (separating-axis triangle–AABB test).
/// Albedo is sampled on the CPU from the glTF baseColor texture / factor at the
/// barycentric UV of the closest point on the triangle to the voxel center.
/// All primitives are merged into one VoxelWorld.
class OC_VOXEL_API MeshVoxelizer {
public:
    /// Compute mesh AABB in meters (union of all transformed primitives).
    [[nodiscard]] static BoundingBox compute_gltf_bounds(const fs::path& gltf_path);

    /// Suggested voxel size = min(AABB extents) / 10.
    [[nodiscard]] static float suggest_voxel_size(const BoundingBox& bounds) noexcept;

    /// Clears @p world, sets voxel_size_meters, and fills solid voxels from the file.
    static MeshVoxelizerResult voxelize_gltf(
        const fs::path& gltf_path,
        float voxel_size_meters,
        VoxelWorld& world,
        VoxelMaterialId solid_material = 1);
};

}// namespace ocarina
