#include "voxel_world.h"
#include "framework/entity_component_system.h"
#include "framework/global_gpu_storage.h"
#include "framework/material.h"
#include "framework/math.h"
#include "framework/mesh.h"
#include "framework/render_component.h"
#include "framework/scene.h"
#include "framework/transform_component.h"
#include "rhi/resources/resource.h"
#include "TaskScheduler.h"
#include "core/logging.h"

namespace ocarina {

namespace {

void shift_axis(int32_t& local, int32_t& chunk_axis, int32_t chunk_size) noexcept {
    if (chunk_size <= 0) {
        return;
    }
    if (local < 0) {
        const int32_t shift = (-local + chunk_size - 1) / chunk_size;
        local += shift * chunk_size;
        chunk_axis -= shift;
    } else if (local >= chunk_size) {
        const int32_t shift = local / chunk_size;
        local -= shift * chunk_size;
        chunk_axis += shift;
    }
}

}// namespace

VoxelWorld::VoxelWorld(uint32_t chunk_size)
    : chunk_size_(chunk_size) {
    OC_ASSERT(chunk_size_ >= 1 && chunk_size_ <= kMaxVoxelChunkSize);
}

VoxelWorld::~VoxelWorld() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Chunk& chunk : chunks_) {
        retire_chunk_meshes_unlocked(chunk);
    }
    retired_meshes_.clear();
}

void VoxelWorld::set_voxel_size_meters(float meters) noexcept {
    voxel_size_meters_ = std::max(meters, 1.0e-6f);
}

void VoxelWorld::create_from_data(const VoxelWorldData& data, const float3& color) {
    if (data.chunk_size == 0 || data.chunk_size > kMaxVoxelChunkSize) {
        OC_ERROR_FORMAT(
            "VoxelWorld::create_from_data: invalid chunk_size {}",
            data.chunk_size);
        return;
    }
    if (data.chunk_size != kVoxelWorldDataChunkSize) {
        OC_ERROR_FORMAT(
            "VoxelWorld::create_from_data: chunk_size {} != ChunkData capacity {}",
            data.chunk_size,
            kVoxelWorldDataChunkSize);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    for (uint32_t entity : published_scene_entities_) {
        ecs.destroy_entity(entity);
    }
    published_scene_entities_.clear();
    for (Chunk& chunk : chunks_) {
        retire_chunk_meshes_unlocked(chunk);
    }
    chunks_.clear();
    coord_to_index_.clear();
    id_to_index_.clear();
    entity_to_merged_.clear();
    next_chunk_id_ = 1;
    flush_retired_meshes_unlocked();

    chunk_size_ = data.chunk_size;
    voxel_size_meters_ = std::max(data.voxel_size_meters, 1.0e-6f);

    const uint32_t volume = data.chunk_size * data.chunk_size * data.chunk_size;
    for (const ChunkData& chunk_data : data.chunks) {
        Chunk& chunk = get_or_create_chunk_unlocked(chunk_data.position);
        chunk.dense_occupancy.assign(chunk_data.voxels, chunk_data.voxels + volume);
        chunk.voxels.clear();
        chunk.clear_cpu_mesh();
        chunk.default_color = color;
        chunk.mesh_dirty = true;
        refresh_chunk_bounds_unlocked(chunk);
    }
}

uint32_t VoxelWorld::chunk_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(chunks_.size());
}

void VoxelWorld::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    for (uint32_t entity : published_scene_entities_) {
        ecs.destroy_entity(entity);
    }
    published_scene_entities_.clear();

    for (Chunk& chunk : chunks_) {
        retire_chunk_meshes_unlocked(chunk);
    }
    chunks_.clear();
    coord_to_index_.clear();
    id_to_index_.clear();
    entity_to_merged_.clear();
    next_chunk_id_ = 1;
    flush_retired_meshes_unlocked();
}

float3 VoxelWorld::chunk_world_origin(int3 chunk_position) const noexcept {
    const float size = static_cast<float>(chunk_size_);
    return make_float3(
        static_cast<float>(chunk_position.x) * size,
        static_cast<float>(chunk_position.y) * size,
        static_cast<float>(chunk_position.z) * size);
}

float3 VoxelWorld::chunk_world_origin_meters(int3 chunk_position) const noexcept {
    return chunk_world_origin(chunk_position) * voxel_size_meters_;
}

float3 VoxelWorld::chunk_world_origin(uint32_t chunk_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Chunk* chunk = find_chunk_unlocked(chunk_id);
    if (chunk == nullptr) {
        return make_float3(0.0f);
    }
    return chunk_world_origin(chunk->position);
}

bool VoxelWorld::set_voxel(
    int3 chunk_position,
    uint3 local,
    bool solid,
    const float3& color,
    Voxel* out_voxel) {
    if (!is_valid_voxel_local(chunk_size_, local)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Chunk& chunk = get_or_create_chunk_unlocked(chunk_position);
    const bool changed = chunk.set_voxel(chunk_size_, local, solid, color);
    if (out_voxel != nullptr) {
        out_voxel->chunk_id = chunk.id;
        out_voxel->position = pack_voxel_local(chunk_size_, local);
        out_voxel->color = chunk.color_at(chunk_size_, local);
        out_voxel->color_samples = solid ? 1u : 0u;
    }
    if (!changed) {
        return true;
    }

    mark_border_neighbors_unlocked(chunk, local);
    refresh_chunk_bounds_unlocked(chunk);
    return true;
}

bool VoxelWorld::set_voxel(
    uint32_t chunk_id,
    uint32_t packed_local,
    bool solid,
    const float3& color) {
    if (!is_valid_packed_voxel_local(chunk_size_, packed_local)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Chunk* chunk = find_chunk_unlocked(chunk_id);
    if (chunk == nullptr) {
        return false;
    }
    const uint3 local = unpack_voxel_local(chunk_size_, packed_local);
    if (!chunk->set_voxel(chunk_size_, local, solid, color)) {
        return solid;
    }
    mark_border_neighbors_unlocked(*chunk, local);
    refresh_chunk_bounds_unlocked(*chunk);
    return true;
}

bool VoxelWorld::set_voxel_world(
    const float3& world_meters,
    bool solid,
    const float3& color) {
    const float inv = 1.0f / voxel_size_meters_;
    const float3 grid = world_meters * inv;
    const int3 cell = make_int3(
        static_cast<int>(std::floor(grid.x)),
        static_cast<int>(std::floor(grid.y)),
        static_cast<int>(std::floor(grid.z)));

    const auto size = static_cast<int32_t>(chunk_size_);
    int3 chunk_pos{};
    int3 local = cell;
    shift_axis(local.x, chunk_pos.x, size);
    shift_axis(local.y, chunk_pos.y, size);
    shift_axis(local.z, chunk_pos.z, size);

    return set_voxel(
        chunk_pos,
        make_uint3(static_cast<uint32_t>(local.x), static_cast<uint32_t>(local.y), static_cast<uint32_t>(local.z)),
        solid,
        color);
}

bool VoxelWorld::is_solid(uint32_t chunk_id, uint32_t packed_local) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Chunk* chunk = find_chunk_unlocked(chunk_id);
    if (chunk == nullptr || !is_valid_packed_voxel_local(chunk_size_, packed_local)) {
        return false;
    }
    return chunk->is_solid(chunk_size_, unpack_voxel_local(chunk_size_, packed_local));
}

bool VoxelWorld::is_solid(int3 chunk_position, uint3 local) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Chunk* chunk = find_chunk_unlocked(chunk_position);
    if (chunk == nullptr) {
        return false;
    }
    return chunk->is_solid(chunk_size_, local);
}

const Chunk* VoxelWorld::find_chunk(int3 chunk_position) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_chunk_unlocked(chunk_position);
}

const Chunk* VoxelWorld::find_chunk(uint32_t chunk_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_chunk_unlocked(chunk_id);
}

Chunk* VoxelWorld::find_chunk(uint32_t chunk_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_chunk_unlocked(chunk_id);
}

void VoxelWorld::rebuild_dirty_meshes() {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_retired_meshes_unlocked();
    rebuild_dirty_meshes_unlocked();
}

void VoxelWorld::rebuild_dirty_meshes(enki::TaskScheduler& scheduler) {
    // Do not hold mutex_ across WaitforTask — that can stall the shared enkiTS pool.
    rebuild_dirty_meshes_parallel(scheduler);
}

void VoxelWorld::publish_to_scene(Scene& scene, Material* material) {
    rebuild_dirty_meshes();

    std::lock_guard<std::mutex> lock(mutex_);
    publish_to_scene_unlocked(scene, material);
}

void VoxelWorld::publish_to_scene(
    Scene& scene,
    Material* material,
    enki::TaskScheduler& scheduler) {
    rebuild_dirty_meshes(scheduler);

    std::lock_guard<std::mutex> lock(mutex_);
    publish_to_scene_unlocked(scene, material);
}

void VoxelWorld::publish_to_scene_unlocked(Scene& scene, Material* material) {
    EntityComponentSystem& ecs = EntityComponentSystem::instance();

    // Rebuild created VoxelRenderer-style entities; drop them but keep the meshes.
    for (Chunk& chunk : chunks_) {
        for (MergedVoxelRender& merged : chunk.merged_renders) {
            if (merged.entity_index == InvalidUI32) {
                continue;
            }
            ecs.destroy_entity(merged.entity_index);
            entity_to_merged_.erase(merged.entity_index);
            merged.entity_index = InvalidUI32;
        }
    }
    for (uint32_t entity : published_scene_entities_) {
        ecs.destroy_entity(entity);
    }
    published_scene_entities_.clear();
    scene.clear_entities();

    for (Chunk& chunk : chunks_) {
        const float3 origin_m = chunk_world_origin_meters(chunk.position);
        for (uint32_t merged_index = 0; merged_index < chunk.merged_renders.size(); ++merged_index) {
            MergedVoxelRender& merged = chunk.merged_renders[merged_index];
            if (merged.mesh == nullptr) {
                continue;
            }
            const uint32_t scene_index = scene.emplace_renderable();
            const uint32_t entity_index = scene.entity_index(scene_index);
            TransformComponent& transform = scene.transform_component(scene_index);
            transform.set_position(origin_m);
            transform.set_scale(make_float3(1.0f, 1.0f, 1.0f));

            RenderComponent& render = scene.render_component(scene_index);
            render.set_mesh(merged.mesh);
            render.set_material(material);
            render.set_update_push_constant_function(
                [origin_m](RenderComponent& render_component, uint32_t) {
                    render_component.set_push_constant_variable("chunk_origin_x", origin_m.x);
                    render_component.set_push_constant_variable("chunk_origin_y", origin_m.y);
                    render_component.set_push_constant_variable("chunk_origin_z", origin_m.z);
                });

            merged.entity_index = entity_index;
            entity_to_merged_.insert_or_assign(
                entity_index,
                MergedRef{chunk.id, merged_index});
            published_scene_entities_.push_back(entity_index);
        }
    }
}

void VoxelWorld::cull_chunks(
    const Frustum& frustum,
    bool frustum_culling_enabled,
    ocarina_vector<uint32_t>& visible_entity_indices) {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_retired_meshes_unlocked();
    rebuild_dirty_meshes_unlocked();

    visible_entity_indices.clear();
    for (const Chunk& chunk : chunks_) {
        if (chunk.merged_renders.empty()) {
            continue;
        }
        BoundingBox meters = chunk.bounds;
        meters.min = meters.min * voxel_size_meters_;
        meters.max = meters.max * voxel_size_meters_;
        if (frustum_culling_enabled && !meters.intersects(frustum)) {
            continue;
        }
        for (const MergedVoxelRender& merged : chunk.merged_renders) {
            visible_entity_indices.push_back(merged.entity_index);
        }
    }
}

uint32_t VoxelWorld::merged_render_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t count = 0;
    for (const Chunk& chunk : chunks_) {
        count += static_cast<uint32_t>(chunk.merged_renders.size());
    }
    return count;
}

uint32_t VoxelWorld::solid_voxel_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t count = 0;
    for (const Chunk& chunk : chunks_) {
        count += chunk.solid_count(chunk_size_);
    }
    return count;
}

BoundingBox VoxelWorld::world_bounds_meters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    BoundingBox bounds;
    for (const Chunk& chunk : chunks_) {
        if (!chunk.bounds.valid ||
            (chunk.voxels.empty() && chunk.dense_occupancy.empty())) {
            continue;
        }
        BoundingBox meters = chunk.bounds;
        meters.min = meters.min * voxel_size_meters_;
        meters.max = meters.max * voxel_size_meters_;
        bounds.merge(meters);
    }
    return bounds;
}

void VoxelWorld::copy_merged_renders(uint32_t chunk_id, ocarina_vector<MergedVoxelRender>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    const Chunk* chunk = find_chunk_unlocked(chunk_id);
    if (chunk == nullptr) {
        return;
    }
    out = chunk->merged_renders;
}

bool VoxelWorld::try_merged_render(uint32_t entity_index, MergedVoxelRender& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entity_to_merged_.find(entity_index);
    if (it == entity_to_merged_.end()) {
        return false;
    }
    const Chunk* chunk = find_chunk_unlocked(it->second.chunk_id);
    if (chunk == nullptr || it->second.merged_index >= chunk->merged_renders.size()) {
        return false;
    }
    out = chunk->merged_renders[it->second.merged_index];
    return true;
}

Chunk* VoxelWorld::find_chunk_unlocked(int3 chunk_position) {
    const auto it = coord_to_index_.find(chunk_position);
    return it == coord_to_index_.end() ? nullptr : &chunks_[it->second];
}

const Chunk* VoxelWorld::find_chunk_unlocked(int3 chunk_position) const {
    const auto it = coord_to_index_.find(chunk_position);
    return it == coord_to_index_.end() ? nullptr : &chunks_[it->second];
}

Chunk* VoxelWorld::find_chunk_unlocked(uint32_t chunk_id) {
    const auto it = id_to_index_.find(chunk_id);
    return it == id_to_index_.end() ? nullptr : &chunks_[it->second];
}

const Chunk* VoxelWorld::find_chunk_unlocked(uint32_t chunk_id) const {
    const auto it = id_to_index_.find(chunk_id);
    return it == id_to_index_.end() ? nullptr : &chunks_[it->second];
}

Chunk& VoxelWorld::get_or_create_chunk_unlocked(int3 chunk_position) {
    if (Chunk* existing = find_chunk_unlocked(chunk_position)) {
        return *existing;
    }

    Chunk chunk{};
    chunk.id = next_chunk_id_++;
    chunk.position = chunk_position;
    refresh_chunk_bounds_unlocked(chunk);

    const uint32_t index = static_cast<uint32_t>(chunks_.size());
    chunks_.push_back(std::move(chunk));
    coord_to_index_.emplace(chunk_position, index);
    id_to_index_.emplace(chunks_.back().id, index);
    return chunks_.back();
}

bool VoxelWorld::sample_solid_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const {
    const auto size = static_cast<int32_t>(chunk_size_);
    shift_axis(x, origin.x, size);
    shift_axis(y, origin.y, size);
    shift_axis(z, origin.z, size);

    const Chunk* chunk = find_chunk_unlocked(origin);
    if (chunk == nullptr) {
        return false;
    }
    return chunk->is_solid(
        chunk_size_,
        make_uint3(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)));
}

float3 VoxelWorld::sample_color_unlocked(int3 origin, int32_t x, int32_t y, int32_t z) const {
    const auto size = static_cast<int32_t>(chunk_size_);
    shift_axis(x, origin.x, size);
    shift_axis(y, origin.y, size);
    shift_axis(z, origin.z, size);

    const Chunk* chunk = find_chunk_unlocked(origin);
    if (chunk == nullptr) {
        return make_float3(1.0f);
    }
    return chunk->color_at(
        chunk_size_,
        make_uint3(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)));
}

void VoxelWorld::rebuild_chunk_cpu_mesh_unlocked(Chunk& chunk) {
    const int3 origin = chunk.position;
    chunk.rebuild_face_mesh(
        chunk_size_,
        [this, origin](int32_t x, int32_t y, int32_t z) noexcept {
            return sample_solid_unlocked(origin, x, y, z);
        });
}

void VoxelWorld::mark_border_neighbors_unlocked(const Chunk& chunk, uint3 local) {
    const uint32_t last = chunk_size_ - 1;
    const int3 coord = chunk.position;
    const auto mark = [&](int32_t x, int32_t y, int32_t z) {
        if (Chunk* neighbor = find_chunk_unlocked(make_int3(x, y, z))) {
            neighbor->mesh_dirty = true;
        }
    };
    if (local.x == 0) {
        mark(coord.x - 1, coord.y, coord.z);
    }
    if (local.x == last) {
        mark(coord.x + 1, coord.y, coord.z);
    }
    if (local.y == 0) {
        mark(coord.x, coord.y - 1, coord.z);
    }
    if (local.y == last) {
        mark(coord.x, coord.y + 1, coord.z);
    }
    if (local.z == 0) {
        mark(coord.x, coord.y, coord.z - 1);
    }
    if (local.z == last) {
        mark(coord.x, coord.y, coord.z + 1);
    }
}

void VoxelWorld::refresh_chunk_bounds_unlocked(Chunk& chunk) noexcept {
    const float3 origin = chunk_world_origin(chunk.position);
    const float extent = static_cast<float>(chunk_size_);
    chunk.bounds.min = origin;
    chunk.bounds.max = origin + make_float3(extent, extent, extent);
    chunk.bounds.valid = true;
}

void VoxelWorld::retire_chunk_meshes_unlocked(Chunk& chunk) {
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    for (const MergedVoxelRender& merged : chunk.merged_renders) {
        if (merged.entity_index != InvalidUI32) {
            ecs.destroy_entity(merged.entity_index);
            entity_to_merged_.erase(merged.entity_index);
        }
        if (merged.mesh == nullptr) {
            continue;
        }
        const auto it = live_meshes_.find(merged.mesh);
        if (it == live_meshes_.end()) {
            continue;
        }
        retired_meshes_.push_back(std::move(it->second));
        live_meshes_.erase(it);
    }
    chunk.merged_renders.clear();
}

void VoxelWorld::flush_retired_meshes_unlocked() {
    ocarina_vector<MeshOwner> still_pending;
    for (MeshOwner& retired : retired_meshes_) {
        const bool upload_finished = !retired.upload_requested ||
            retired.mesh->gpu_resource_state() == GPUResourceState::GPU_Ready;
        if (!upload_finished) {
            still_pending.push_back(std::move(retired));
        }
    }
    retired_meshes_ = std::move(still_pending);
}

void VoxelWorld::apply_chunk_mesh_batches_unlocked(
    Chunk& chunk,
    ocarina_vector<VoxelMergedBatch>&& batches) {
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    const bool can_upload = GlobalGPUStorage::instance().device() != nullptr;
    const float3 origin_position = chunk_world_origin_meters(chunk.position);
    const float scale = voxel_size_meters_;

    for (VoxelMergedBatch& batch : batches) {
        if (batch.geometry.positions.empty() || batch.geometry.indices.empty()) {
            continue;
        }

        // CPU face mesh is in cell space; upload meters so voxel_mesh.vert can add origin.
        if (scale != 1.0f) {
            for (Vector3& position : batch.geometry.positions) {
                position.x *= scale;
                position.y *= scale;
                position.z *= scale;
            }
            if (batch.local_bounds.valid) {
                batch.local_bounds.min = batch.local_bounds.min * scale;
                batch.local_bounds.max = batch.local_bounds.max * scale;
            }
        }

        unique_ptr<Mesh> mesh_owner(new_with_allocator<Mesh>());
        Mesh* mesh = mesh_owner.get();
        if (batch.local_bounds.valid) {
            mesh->set_local_bounds(batch.local_bounds.min, batch.local_bounds.max);
        }

        bool upload_requested = false;
        if (can_upload) {
            GlobalGPUStorage::instance().upload_mesh(std::move(batch.geometry), mesh);
            upload_requested = true;
        }

        const uint32_t entity_index = ecs.create_entity();
        RenderComponent& render = ecs.add_render_component(entity_index);
        render.set_mesh(mesh);
        render.set_update_push_constant_function(
            [origin_position](RenderComponent& render_component, uint32_t) {
                render_component.set_push_constant_variable("chunk_origin_x", origin_position.x);
                render_component.set_push_constant_variable("chunk_origin_y", origin_position.y);
                render_component.set_push_constant_variable("chunk_origin_z", origin_position.z);
            });

        MergedVoxelRender merged{};
        merged.entity_index = entity_index;
        merged.chunk_id = chunk.id;
        merged.voxel_count = batch.voxel_count;
        merged.quad_count = batch.quad_count;
        merged.mesh = mesh;

        const auto merged_index = static_cast<uint32_t>(chunk.merged_renders.size());
        chunk.merged_renders.push_back(merged);
        entity_to_merged_.insert_or_assign(
            entity_index,
            MergedRef{chunk.id, merged_index});
        live_meshes_.insert_or_assign(
            mesh,
            MeshOwner{std::move(mesh_owner), upload_requested});
    }

    chunk.mesh_dirty = false;
}

void VoxelWorld::rebuild_dirty_meshes_unlocked() {
    for (Chunk& chunk : chunks_) {
        if (!chunk.mesh_dirty) {
            continue;
        }

        retire_chunk_meshes_unlocked(chunk);
        rebuild_chunk_cpu_mesh_unlocked(chunk);

        VoxelMergedBatch batch;
        chunk.build_mesh_batch(batch);
        ocarina_vector<VoxelMergedBatch> batches;
        if (!batch.geometry.positions.empty()) {
            batches.push_back(std::move(batch));
        }
        apply_chunk_mesh_batches_unlocked(chunk, std::move(batches));
    }
}

void VoxelWorld::rebuild_dirty_meshes_parallel(enki::TaskScheduler& scheduler) {
    ocarina_vector<uint32_t> dirty_indices;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_retired_meshes_unlocked();
        dirty_indices.reserve(chunks_.size());
        for (uint32_t i = 0; i < chunks_.size(); ++i) {
            if (chunks_[i].mesh_dirty) {
                dirty_indices.push_back(i);
                retire_chunk_meshes_unlocked(chunks_[i]);
            }
        }
    }
    if (dirty_indices.empty()) {
        return;
    }

    struct ChunkMeshJob {
        VoxelMergedBatch batch;
        bool has_batch = false;
    };
    ocarina_vector<ChunkMeshJob> jobs(dirty_indices.size());

    struct MeshTask : enki::ITaskSet {
        VoxelWorld* world = nullptr;
        const ocarina_vector<uint32_t>* dirty_indices = nullptr;
        ocarina_vector<ChunkMeshJob>* jobs = nullptr;

        void ExecuteRange(enki::TaskSetPartition range, uint32_t) override {
            for (uint32_t job_index = range.start; job_index < range.end; ++job_index) {
                const uint32_t chunk_index = (*dirty_indices)[job_index];
                Chunk& chunk = world->chunks_[chunk_index];
                world->rebuild_chunk_cpu_mesh_unlocked(chunk);
                chunk.build_mesh_batch((*jobs)[job_index].batch);
                (*jobs)[job_index].has_batch =
                    !(*jobs)[job_index].batch.geometry.positions.empty();
            }
        }
    };

    MeshTask task;
    task.world = this;
    task.dirty_indices = &dirty_indices;
    task.jobs = &jobs;
    task.m_SetSize = static_cast<uint32_t>(dirty_indices.size());
    task.m_MinRange = 1;
    scheduler.AddTaskSetToPipe(&task);
    scheduler.WaitforTask(&task);

    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t job_index = 0; job_index < dirty_indices.size(); ++job_index) {
        ocarina_vector<VoxelMergedBatch> batches;
        if (jobs[job_index].has_batch) {
            batches.push_back(std::move(jobs[job_index].batch));
        }
        apply_chunk_mesh_batches_unlocked(
            chunks_[dirty_indices[job_index]],
            std::move(batches));
    }
}

void VoxelWorld::rebuild_dirty_meshes_parallel_unlocked(enki::TaskScheduler& scheduler) {
    // Prefer rebuild_dirty_meshes_parallel; kept for call sites that already hold no lock.
    rebuild_dirty_meshes_parallel(scheduler);
}

}// namespace ocarina
