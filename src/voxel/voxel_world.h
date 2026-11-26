#pragma once

#include "chunk.h"
#include "voxel_world_data.h"
#include "framework/frustum.h"
#include "core/stl.h"
#include <mutex>

namespace enki {
class TaskScheduler;
}

namespace ocarina {

class Material;
class Scene;
class VoxelRenderer;

/// Owns chunks, face meshing, and chunk frustum culling.
/// All solids share one Material (voxel_mesh); appearance comes from vertex color.
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

    /// Clears and rebuilds this world from dense chunk occupancy.
    /// Copies each ChunkData into Chunk::dense_occupancy for O(1) meshing samples.
    void create_from_data(const VoxelWorldData& data, const float3& color = make_float3(1.0f));

    /// Creates the chunk when the cell is written. `local` must be in [0, chunk_size).
    /// `solid == true` inserts/updates; `solid == false` clears air.
    bool set_voxel(
        int3 chunk_position,
        uint3 local,
        bool solid = true,
        const float3& color = make_float3(1.0f),
        Voxel* out_voxel = nullptr);
    bool set_voxel(
        uint32_t chunk_id,
        uint32_t packed_local,
        bool solid = true,
        const float3& color = make_float3(1.0f));

    /// World-space meters → chunk/local using voxel_size_meters_.
    bool set_voxel_world(
        const float3& world_meters,
        bool solid = true,
        const float3& color = make_float3(1.0f));

    [[nodiscard]] bool is_solid(uint32_t chunk_id, uint32_t packed_local) const;
    [[nodiscard]] bool is_solid(int3 chunk_position, uint3 local) const;

    [[nodiscard]] const Chunk* find_chunk(int3 chunk_position) const;
    [[nodiscard]] const Chunk* find_chunk(uint32_t chunk_id) const;
    [[nodiscard]] Chunk* find_chunk(uint32_t chunk_id);

    void rebuild_dirty_meshes();
    /// Rebuilds exposed-face meshes for dirty chunks in parallel (enkiTS).
    void rebuild_dirty_meshes(enki::TaskScheduler& scheduler);

    /// Rebuild meshes, then replace @p scene with one renderable per chunk mesh.
    /// Transform = chunk origin in meters; mesh verts are scaled to meters on upload.
    void publish_to_scene(Scene& scene, Material* material);
    void publish_to_scene(Scene& scene, Material* material, enki::TaskScheduler& scheduler);

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

    [[nodiscard]] bool sample_solid_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] float3 sample_color_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const;
    void rebuild_chunk_cpu_mesh_unlocked(Chunk& chunk);
    void mark_border_neighbors_unlocked(const Chunk& chunk, uint3 local);
    void refresh_chunk_bounds_unlocked(Chunk& chunk) noexcept;
    void retire_chunk_meshes_unlocked(Chunk& chunk);
    void flush_retired_meshes_unlocked();
    void rebuild_dirty_meshes_unlocked();
    void rebuild_dirty_meshes_parallel(enki::TaskScheduler& scheduler);
    void rebuild_dirty_meshes_parallel_unlocked(enki::TaskScheduler& scheduler);
    void apply_chunk_mesh_batches_unlocked(
        Chunk& chunk,
        ocarina_vector<VoxelMergedBatch>&& batches);
    void publish_to_scene_unlocked(Scene& scene, Material* material);

    uint32_t chunk_size_ = kDefaultVoxelChunkSize;
    float voxel_size_meters_ = 1.0f;
    uint32_t next_chunk_id_ = 1;

    ocarina_vector<Chunk> chunks_;
    ocarina_unordered_map<int3, uint32_t, ChunkCoordHash, ChunkCoordEqual> coord_to_index_;
    ocarina_unordered_map<uint32_t, uint32_t> id_to_index_;
    ocarina_unordered_map<Mesh*, MeshOwner> live_meshes_;
    ocarina_vector<MeshOwner> retired_meshes_;
    ocarina_unordered_map<uint32_t, MergedRef> entity_to_merged_;
    ocarina_vector<uint32_t> published_scene_entities_;

    mutable std::mutex mutex_;
};

}// namespace ocarina
