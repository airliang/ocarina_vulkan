#include "voxel/voxel_world.h"
#include "voxel/voxel_renderer.h"
#include "framework/entity_component_system.h"
#include "framework/frustum.h"
#include "framework/math.h"
#include "framework/mesh.h"
#include "framework/render_component.h"

#include <cstdio>
#include <cmath>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    ++g_failures;
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

uint32_t quad_count(const ocarina::ocarina_vector<ocarina::VoxelGreedyQuad>& quads) {
    return static_cast<uint32_t>(quads.size());
}

bool has_quad_extent(
    const ocarina::ocarina_vector<ocarina::VoxelGreedyQuad>& quads,
    uint32_t width,
    uint32_t height) {
    for (const ocarina::VoxelGreedyQuad& quad : quads) {
        if (quad.width == width && quad.height == height) {
            return true;
        }
        if (quad.width == height && quad.height == width) {
            return true;
        }
    }
    return false;
}

void test_same_material_merges_into_one_render() {
    constexpr uint32_t kChunkSize = 8;
    ocarina::VoxelWorld world(kChunkSize);

    ocarina::Voxel first{};
    ocarina::Voxel second{};
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(0, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &first));
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(1, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &second));
    EXPECT(first.chunk_id != ocarina::kInvalidChunkId);
    EXPECT(first.chunk_id == second.chunk_id);
    EXPECT(ocarina::unpack_voxel_local(kChunkSize, first.position).x == 0);
    EXPECT(ocarina::unpack_voxel_local(kChunkSize, second.position).x == 1);
    EXPECT(world.voxel_material(first.chunk_id, first.position) == 1);
    EXPECT(world.voxel_material(second.chunk_id, second.position) == 1);

    ocarina::ocarina_vector<ocarina::VoxelGreedyQuad> quads;
    world.copy_greedy_quads(first.chunk_id, quads);
    EXPECT(quad_count(quads) == 6);
    EXPECT(has_quad_extent(quads, 2, 1));

    world.rebuild_dirty_meshes();
    EXPECT(world.merged_render_count() == 1);

    ocarina::ocarina_vector<ocarina::MergedVoxelRender> renders;
    world.copy_merged_renders(first.chunk_id, renders);
    EXPECT(renders.size() == 1);
    EXPECT(renders[0].voxel_count == 2);
    EXPECT(renders[0].quad_count == 6);
    EXPECT(renders[0].material_id == 1);
    EXPECT(renders[0].mesh != nullptr);
    EXPECT(renders[0].mesh->has_local_bounds());
    EXPECT(renders[0].mesh->get_local_bounds().max.x == 2.0f);

    ocarina::RenderComponent* render =
        ocarina::EntityComponentSystem::instance().try_render_component(renders[0].entity_index);
    EXPECT(render != nullptr);
    EXPECT(render->get_mesh() == renders[0].mesh);
    EXPECT(!ocarina::EntityComponentSystem::instance().has_transform_component(renders[0].entity_index));
    EXPECT(ocarina::EntityComponentSystem::instance().alive_entity_count() == 1);

    const ocarina::Chunk* chunk = world.find_chunk(first.chunk_id);
    EXPECT(chunk != nullptr);
    EXPECT(chunk->voxels.size() == 2);
    EXPECT(chunk->position.x == 0 && chunk->position.y == 0 && chunk->position.z == 0);
}

void test_different_materials_keep_the_shared_face() {
    ocarina::VoxelWorld world(8);
    ocarina::Voxel first{};
    ocarina::Voxel second{};
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(0, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &first));
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(1, 0, 0),
        2,
        ocarina::make_float3(1.0f),
        &second));

    ocarina::ocarina_vector<ocarina::VoxelGreedyQuad> quads;
    world.copy_greedy_quads(first.chunk_id, quads);
    EXPECT(quad_count(quads) == 12);
    EXPECT(!has_quad_extent(quads, 2, 1));

    world.rebuild_dirty_meshes();
    EXPECT(world.merged_render_count() == 2);
}

void test_local_coordinate_is_chunk_cell() {
    constexpr uint32_t kChunkSize = 4;
    ocarina::VoxelWorld world(kChunkSize);
    EXPECT(!world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(kChunkSize, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        nullptr));
    EXPECT(world.chunk_count() == 0);

    ocarina::Voxel corner{};
    const ocarina::uint3 last = ocarina::make_uint3(kChunkSize - 1, kChunkSize - 1, kChunkSize - 1);
    EXPECT(world.set_voxel(
        ocarina::make_int3(2, -3, 4),
        last,
        3,
        ocarina::make_float3(1.0f),
        &corner));
    EXPECT(ocarina::unpack_voxel_local(kChunkSize, corner.position).x == kChunkSize - 1);
    EXPECT(ocarina::unpack_voxel_local(kChunkSize, corner.position).y == kChunkSize - 1);
    EXPECT(ocarina::unpack_voxel_local(kChunkSize, corner.position).z == kChunkSize - 1);

    const ocarina::Chunk* chunk = world.find_chunk(corner.chunk_id);
    EXPECT(chunk != nullptr);
    EXPECT(chunk->position.x == 2 && chunk->position.y == -3 && chunk->position.z == 4);
    EXPECT(world.voxel_material(ocarina::make_int3(2, -3, 4), last) == 3);
}

void test_cross_chunk_same_material_drops_border_face() {
    constexpr uint32_t kChunkSize = 4;
    ocarina::VoxelWorld world(kChunkSize);
    ocarina::Voxel left{};
    ocarina::Voxel right{};
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(kChunkSize - 1, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &left));
    EXPECT(world.set_voxel(
        ocarina::make_int3(1, 0, 0),
        ocarina::make_uint3(0, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &right));
    EXPECT(left.chunk_id != right.chunk_id);

    ocarina::ocarina_vector<ocarina::VoxelGreedyQuad> left_quads;
    ocarina::ocarina_vector<ocarina::VoxelGreedyQuad> right_quads;
    world.copy_greedy_quads(left.chunk_id, left_quads);
    world.copy_greedy_quads(right.chunk_id, right_quads);
    EXPECT(quad_count(left_quads) == 5);
    EXPECT(quad_count(right_quads) == 5);

    world.rebuild_dirty_meshes();
    EXPECT(world.merged_render_count() == 2);
}

void test_chunk_frustum_cull() {
    constexpr uint32_t kChunkSize = 16;
    ocarina::VoxelWorld world(kChunkSize);
    ocarina::Voxel near_voxel{};
    ocarina::Voxel far_voxel{};
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 0),
        ocarina::make_uint3(0, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &near_voxel));
    EXPECT(world.set_voxel(
        ocarina::make_int3(0, 0, 40),
        ocarina::make_uint3(0, 0, 0),
        1,
        ocarina::make_float3(1.0f),
        &far_voxel));

    const ktm::fmat4x4 view = ocarina::look_at_lh(
        ktm::fvec3{8.0f, 8.0f, -20.0f},
        ktm::fvec3{8.0f, 8.0f, 8.0f},
        ktm::fvec3{0.0f, 1.0f, 0.0f});
    const ktm::fmat4x4 projection = ocarina::perspective_lh(
        60.0f * 3.14159265f / 180.0f,
        1.0f,
        0.1f,
        80.0f);
    const ocarina::Frustum frustum(projection * view);

    const ocarina::Chunk* near_chunk = world.find_chunk(near_voxel.chunk_id);
    const ocarina::Chunk* far_chunk = world.find_chunk(far_voxel.chunk_id);
    EXPECT(near_chunk != nullptr && near_chunk->bounds.intersects(frustum));
    EXPECT(far_chunk != nullptr && !far_chunk->bounds.intersects(frustum));

    ocarina::ocarina_vector<uint32_t> visible;
    world.cull_chunks(frustum, true, visible);
    EXPECT(visible.size() == 1);
    if (visible.size() == 1) {
        ocarina::MergedVoxelRender merged{};
        EXPECT(world.try_merged_render(visible[0], merged));
        EXPECT(merged.chunk_id == near_voxel.chunk_id);
        EXPECT(merged.voxel_count == 1);
        EXPECT(merged.quad_count == 6);
    }

    const ocarina::float3 origin = world.chunk_world_origin(near_voxel.chunk_id);
    EXPECT(origin.x == 0.0f && origin.y == 0.0f && origin.z == 0.0f);
}

void test_voxel_renderer_type_exists() {
    // Compile-time / link smoke: VoxelRenderer is the voxel draw path, not Renderer.
    EXPECT(sizeof(ocarina::VoxelRenderer) > sizeof(void*));
}

}// namespace

int main() {
    test_same_material_merges_into_one_render();
    test_different_materials_keep_the_shared_face();
    test_local_coordinate_is_chunk_cell();
    test_cross_chunk_same_material_drops_border_face();
    test_chunk_frustum_cull();
    test_voxel_renderer_type_exists();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d voxel core checks failed\n", g_failures);
        return 1;
    }
    std::printf("voxel core checks passed\n");
    return 0;
}
