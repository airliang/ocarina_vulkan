#include "voxel_mesher.h"
#include "framework/math.h"
#include "rhi/vertex_buffer.h"

namespace ocarina {

namespace {

constexpr uint32_t kMaxVerticesPerMesh = 65532;

void axes_for(uint8_t axis, int& u_axis, int& v_axis) noexcept {
    if (axis == 0) {
        u_axis = 1;
        v_axis = 2;
    } else if (axis == 1) {
        u_axis = 0;
        v_axis = 2;
    } else {
        u_axis = 0;
        v_axis = 1;
    }
}

void fill_mask(
    uint32_t chunk_size,
    uint8_t axis,
    int8_t sign,
    uint32_t slice,
    const ocarina::function<VoxelMaterialId(int32_t, int32_t, int32_t)>& sample,
    ocarina_vector<VoxelMaterialId>& mask) {
    int u_axis = 0;
    int v_axis = 0;
    axes_for(axis, u_axis, v_axis);

    mask.assign(static_cast<size_t>(chunk_size) * chunk_size, kVoxelAir);
    const int32_t neighbor_slice = static_cast<int32_t>(slice) + (sign > 0 ? 1 : -1);

    for (uint32_t v = 0; v < chunk_size; ++v) {
        for (uint32_t u = 0; u < chunk_size; ++u) {
            int32_t cell[3]{};
            int32_t neighbor[3]{};
            cell[axis] = static_cast<int32_t>(slice);
            neighbor[axis] = neighbor_slice;
            cell[u_axis] = neighbor[u_axis] = static_cast<int32_t>(u);
            cell[v_axis] = neighbor[v_axis] = static_cast<int32_t>(v);

            const VoxelMaterialId material = sample(cell[0], cell[1], cell[2]);
            if (material == kVoxelAir) {
                continue;
            }
            if (sample(neighbor[0], neighbor[1], neighbor[2]) == material) {
                continue;
            }
            mask[static_cast<size_t>(v) * chunk_size + u] = material;
        }
    }
}

void greedy_emit(
    uint32_t chunk_size,
    uint8_t axis,
    int8_t sign,
    uint32_t slice,
    ocarina_vector<VoxelMaterialId>& mask,
    const ocarina::function<float3(int32_t, int32_t, int32_t)>& sample_color,
    ocarina_vector<VoxelGreedyQuad>& quads) {
    int u_axis = 0;
    int v_axis = 0;
    axes_for(axis, u_axis, v_axis);

    for (uint32_t v = 0; v < chunk_size; ++v) {
        for (uint32_t u = 0; u < chunk_size;) {
            const VoxelMaterialId material = mask[static_cast<size_t>(v) * chunk_size + u];
            if (material == kVoxelAir) {
                ++u;
                continue;
            }

            uint32_t width = 1;
            while (u + width < chunk_size &&
                   mask[static_cast<size_t>(v) * chunk_size + (u + width)] == material) {
                ++width;
            }

            uint32_t height = 1;
            bool stop = false;
            while (v + height < chunk_size && !stop) {
                for (uint32_t column = 0; column < width; ++column) {
                    if (mask[static_cast<size_t>(v + height) * chunk_size + (u + column)] != material) {
                        stop = true;
                        break;
                    }
                }
                if (!stop) {
                    ++height;
                }
            }

            float3 color_sum = make_float3(0.0f);
            uint32_t color_count = 0;
            for (uint32_t row = 0; row < height; ++row) {
                for (uint32_t column = 0; column < width; ++column) {
                    int32_t cell[3]{};
                    cell[axis] = static_cast<int32_t>(slice);
                    cell[u_axis] = static_cast<int32_t>(u + column);
                    cell[v_axis] = static_cast<int32_t>(v + row);
                    color_sum += sample_color(cell[0], cell[1], cell[2]);
                    ++color_count;
                }
            }

            VoxelGreedyQuad quad{};
            quad.axis = axis;
            quad.sign = sign;
            quad.u = u;
            quad.v = v;
            quad.slice = slice;
            quad.width = width;
            quad.height = height;
            quad.material = material;
            quad.color = color_count > 0
                ? color_sum / static_cast<float>(color_count)
                : make_float3(1.0f);
            quads.push_back(quad);

            for (uint32_t row = 0; row < height; ++row) {
                for (uint32_t column = 0; column < width; ++column) {
                    mask[static_cast<size_t>(v + row) * chunk_size + (u + column)] = kVoxelAir;
                }
            }
            u += width;
        }
    }
}

void append_vertex(
    OwnedMeshGeometry& geometry,
    BoundingBox& bounds,
    const Vector3& position,
    const Vector3& normal,
    const Vector2& uv,
    const float3& color) {
    geometry.positions.push_back(position);
    geometry.normals.push_back(normal);
    geometry.uvs.push_back(uv);
    geometry.colors.push_back(Vector4{color.x, color.y, color.z, 1.0f});
    bounds.expand(make_float3(position.x, position.y, position.z));
}

void append_quad(VoxelMergedBatch& batch, const VoxelGreedyQuad& quad) {
    const float u0 = static_cast<float>(quad.u);
    const float v0 = static_cast<float>(quad.v);
    const float u1 = static_cast<float>(quad.u + quad.width);
    const float v1 = static_cast<float>(quad.v + quad.height);
    const float plane = (quad.sign > 0)
        ? static_cast<float>(quad.slice + 1)
        : static_cast<float>(quad.slice);

    Vector3 corners[4]{};
    Vector3 normal{};
    if (quad.axis == 0 && quad.sign > 0) {
        normal = Vector3{1.0f, 0.0f, 0.0f};
        corners[0] = Vector3{plane, u0, v1};
        corners[1] = Vector3{plane, u0, v0};
        corners[2] = Vector3{plane, u1, v0};
        corners[3] = Vector3{plane, u1, v1};
    } else if (quad.axis == 0) {
        normal = Vector3{-1.0f, 0.0f, 0.0f};
        corners[0] = Vector3{plane, u0, v0};
        corners[1] = Vector3{plane, u0, v1};
        corners[2] = Vector3{plane, u1, v1};
        corners[3] = Vector3{plane, u1, v0};
    } else if (quad.axis == 1 && quad.sign > 0) {
        normal = Vector3{0.0f, 1.0f, 0.0f};
        corners[0] = Vector3{u0, plane, v1};
        corners[1] = Vector3{u1, plane, v1};
        corners[2] = Vector3{u1, plane, v0};
        corners[3] = Vector3{u0, plane, v0};
    } else if (quad.axis == 1) {
        normal = Vector3{0.0f, -1.0f, 0.0f};
        corners[0] = Vector3{u0, plane, v0};
        corners[1] = Vector3{u1, plane, v0};
        corners[2] = Vector3{u1, plane, v1};
        corners[3] = Vector3{u0, plane, v1};
    } else if (quad.sign > 0) {
        normal = Vector3{0.0f, 0.0f, 1.0f};
        corners[0] = Vector3{u0, v0, plane};
        corners[1] = Vector3{u1, v0, plane};
        corners[2] = Vector3{u1, v1, plane};
        corners[3] = Vector3{u0, v1, plane};
    } else {
        normal = Vector3{0.0f, 0.0f, -1.0f};
        corners[0] = Vector3{u1, v0, plane};
        corners[1] = Vector3{u0, v0, plane};
        corners[2] = Vector3{u0, v1, plane};
        corners[3] = Vector3{u1, v1, plane};
    }

    OwnedMeshGeometry& geometry = batch.geometry;
    const auto base32 = static_cast<uint32_t>(geometry.positions.size());
    OC_ASSERT(base32 + 4 <= kMaxVerticesPerMesh);
    const auto base = static_cast<uint16_t>(base32);

    append_vertex(geometry, batch.local_bounds, corners[0], normal, Vector2{0.0f, v1 - v0}, quad.color);
    append_vertex(geometry, batch.local_bounds, corners[1], normal, Vector2{u1 - u0, v1 - v0}, quad.color);
    append_vertex(geometry, batch.local_bounds, corners[2], normal, Vector2{u1 - u0, 0.0f}, quad.color);
    append_vertex(geometry, batch.local_bounds, corners[3], normal, Vector2{0.0f, 0.0f}, quad.color);

    geometry.indices.push_back(base + 0);
    geometry.indices.push_back(base + 1);
    geometry.indices.push_back(base + 2);
    geometry.indices.push_back(base + 0);
    geometry.indices.push_back(base + 2);
    geometry.indices.push_back(base + 3);
    ++batch.quad_count;
}

[[nodiscard]] uint32_t open_batch(
    ocarina_vector<VoxelMergedBatch>& batches,
    VoxelMaterialId material) {
    for (uint32_t index = static_cast<uint32_t>(batches.size()); index > 0; --index) {
        VoxelMergedBatch& batch = batches[index - 1];
        if (batch.material == material &&
            batch.geometry.positions.size() + 4 <= kMaxVerticesPerMesh) {
            return index - 1;
        }
    }

    VoxelMergedBatch batch{};
    batch.material = material;
    batch.local_bounds.reset();
    batches.push_back(std::move(batch));
    return static_cast<uint32_t>(batches.size() - 1);
}

}// namespace

void build_voxel_greedy_quads(
    uint32_t chunk_size,
    const ocarina::function<VoxelMaterialId(int32_t x, int32_t y, int32_t z)>& sample,
    const ocarina::function<float3(int32_t x, int32_t y, int32_t z)>& sample_color,
    ocarina_vector<VoxelGreedyQuad>& quads) {
    OC_ASSERT(chunk_size >= 1);
    quads.clear();

    ocarina_vector<VoxelMaterialId> mask;
    mask.reserve(static_cast<size_t>(chunk_size) * chunk_size);

    for (uint8_t axis = 0; axis < 3; ++axis) {
        for (int8_t sign : {int8_t{1}, int8_t{-1}}) {
            for (uint32_t slice = 0; slice < chunk_size; ++slice) {
                fill_mask(chunk_size, axis, sign, slice, sample, mask);
                greedy_emit(chunk_size, axis, sign, slice, mask, sample_color, quads);
            }
        }
    }
}

void build_voxel_merged_batches(
    const ocarina_vector<VoxelGreedyQuad>& quads,
    const ocarina_unordered_map<VoxelMaterialId, uint32_t>& voxel_counts,
    ocarina_vector<VoxelMergedBatch>& batches) {
    batches.clear();
    for (const VoxelGreedyQuad& quad : quads) {
        if (quad.material == kVoxelAir || quad.width == 0 || quad.height == 0) {
            continue;
        }
        const uint32_t index = open_batch(batches, quad.material);
        append_quad(batches[index], quad);
    }

    for (VoxelMergedBatch& batch : batches) {
        const auto it = voxel_counts.find(batch.material);
        batch.voxel_count = it == voxel_counts.end() ? 0u : it->second;
    }
}

}// namespace ocarina
