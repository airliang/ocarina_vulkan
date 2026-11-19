#pragma once

#include "chunk.h"
#include "voxel_mesher.h"
#include "framework/frustum.h"
#include "core/stl.h"
#include <mutex>

namespace ocarina {

class Material;
class Scene;
class VoxelRenderer;

/// Owns chunks, greedy meshing, and chunk frustum culling.
class OC_VOXEL_API VoxelWorld {
public:
    explicit VoxelWorld(uint32_t chunk_size = kDefaultVoxelChunkSize);
    ~VoxelWorld();

    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;

    [[nodiscard]] uint32_t chunk_size() const noexcept { return chunk_size_; }
    [[nodiscard]] uint32_t chunk_count() const;
    [[nodiscard]] float voxel_size_meters() const noexcept { return voxel_size_meters_; }
    void set_voxel_size_meters(float meters) noexcept;

    [[nodiscard]] const ocarina_vector<Chunk>& chunks() const noexcept { return chunks_; }

    void clear();

    /// Creates the chunk when the cell is written. `local` must be in [0, chunk_size).
    bool set_voxel(
        int3 chunk_position,
        uint3 local,
        VoxelMaterialId material,
        const float3& color = make_float3(1.0f),
        Voxel* out_voxel = nullptr);
    bool set_voxel(
        uint32_t chunk_id,
        uint32_t packed_local,
        VoxelMaterialId material,
        const float3& color = make_float3(1.0f));

    /// World-space meters → chunk/local using voxel_size_meters_.
    bool set_voxel_world(const float3& world_meters, VoxelMaterialId material, const float3& color);

    [[nodiscard]] VoxelMaterialId voxel_material(uint32_t chunk_id, uint32_t packed_local) const;
    [[nodiscard]] VoxelMaterialId voxel_material(int3 chunk_position, uint3 local) const;

    [[nodiscard]] const Chunk* find_chunk(int3 chunk_position) const;
    [[nodiscard]] const Chunk* find_chunk(uint32_t chunk_id) const;
    [[nodiscard]] Chunk* find_chunk(uint32_t chunk_id);

    void bind_material(VoxelMaterialId material_id, Material* material);

    void rebuild_dirty_meshes();

    /// Rebuild meshes, then replace @p scene with one renderable per merged chunk mesh.
    /// Transform = chunk origin in meters, scale = voxel_size_meters (mesh verts stay in cells).
    /// Uses mesh.vert push constant transform_index (not voxel_mesh.vert).
    void publish_to_scene(Scene& scene, Material* material);

    void cull_chunks(
        const Frustum& frustum,
        bool frustum_culling_enabled,
        ocarina_vector<uint32_t>& visible_entity_indices);

    /// Cell-space origin (multiply by voxel_size_meters for meters).
    [[nodiscard]] float3 chunk_world_origin(uint32_t chunk_id) const;
    [[nodiscard]] float3 chunk_world_origin(int3 chunk_position) const noexcept;
    [[nodiscard]] float3 chunk_world_origin_meters(int3 chunk_position) const noexcept;

    [[nodiscard]] uint32_t merged_render_count() const;
    [[nodiscard]] uint32_t solid_voxel_count() const;
    void copy_merged_renders(uint32_t chunk_id, ocarina_vector<MergedVoxelRender>& out) const;
    [[nodiscard]] bool try_merged_render(uint32_t entity_index, MergedVoxelRender& out) const;
    void copy_greedy_quads(uint32_t chunk_id, ocarina_vector<VoxelGreedyQuad>& out) const;

    [[nodiscard]] BoundingBox world_bounds_meters() const;

private:
    friend class VoxelRenderer;

    struct MeshOwner {
        unique_ptr<Mesh> mesh;
        bool upload_requested = false;
    };

    struct MergedRef {
        uint32_t chunk_id = kInvalidChunkId;
        uint32_t merged_index = 0;
    };

    [[nodiscard]] Chunk* find_chunk_unlocked(int3 chunk_position);
    [[nodiscard]] const Chunk* find_chunk_unlocked(int3 chunk_position) const;
    [[nodiscard]] Chunk* find_chunk_unlocked(uint32_t chunk_id);
    [[nodiscard]] const Chunk* find_chunk_unlocked(uint32_t chunk_id) const;
    Chunk& get_or_create_chunk_unlocked(int3 chunk_position);

    [[nodiscard]] VoxelMaterialId sample_material_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] float3 sample_color_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const;
    void mark_border_neighbors_unlocked(const Chunk& chunk, uint3 local);
    void refresh_chunk_bounds_unlocked(Chunk& chunk) noexcept;
    void retire_chunk_meshes_unlocked(Chunk& chunk);
    void flush_retired_meshes_unlocked();
    void rebuild_dirty_meshes_unlocked();

    uint32_t chunk_size_ = kDefaultVoxelChunkSize;
    float voxel_size_meters_ = 1.0f;
    uint32_t next_chunk_id_ = 1;

    ocarina_vector<Chunk> chunks_;
    ocarina_unordered_map<int3, uint32_t, ChunkCoordHash, ChunkCoordEqual> coord_to_index_;
    ocarina_unordered_map<uint32_t, uint32_t> id_to_index_;
    ocarina_unordered_map<VoxelMaterialId, Material*> materials_;
    ocarina_unordered_map<Mesh*, MeshOwner> live_meshes_;
    ocarina_vector<MeshOwner> retired_meshes_;
    ocarina_unordered_map<uint32_t, MergedRef> entity_to_merged_;
    ocarina_vector<uint32_t> published_scene_entities_;

    mutable std::mutex mutex_;
};

}// namespace ocarina
