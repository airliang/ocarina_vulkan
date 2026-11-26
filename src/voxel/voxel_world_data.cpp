#include "voxel_world_data.h"
#include "core/logging.h"
#include <algorithm>

namespace ocarina {

const ChunkData* find_chunk_data(const VoxelWorldData& data, int3 chunk_position) noexcept {
    for (const ChunkData& chunk : data.chunks) {
        if (chunk.position.x == chunk_position.x &&
            chunk.position.y == chunk_position.y &&
            chunk.position.z == chunk_position.z) {
            return &chunk;
        }
    }
    return nullptr;
}

uint8_t sample_voxel_world_data(
    const VoxelWorldData& data,
    int32_t world_x,
    int32_t world_y,
    int32_t world_z) noexcept {
    const auto size = static_cast<int32_t>(data.chunk_size);
    if (size <= 0) {
        return 0;
    }

    int3 chunk_pos{};
    int32_t lx = world_x;
    int32_t ly = world_y;
    int32_t lz = world_z;

    auto shift = [size](int32_t& local, int32_t& chunk_axis) {
        if (local < 0) {
            const int32_t shift_n = (-local + size - 1) / size;
            local += shift_n * size;
            chunk_axis -= shift_n;
        } else if (local >= size) {
            const int32_t shift_n = local / size;
            local -= shift_n * size;
            chunk_axis += shift_n;
        }
    };
    shift(lx, chunk_pos.x);
    shift(ly, chunk_pos.y);
    shift(lz, chunk_pos.z);

    const ChunkData* chunk = find_chunk_data(data, chunk_pos);
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->voxels[chunk_data_local_index(
        data.chunk_size,
        static_cast<uint32_t>(lx),
        static_cast<uint32_t>(ly),
        static_cast<uint32_t>(lz))];
}

VoxelWorldData build_voxel_world_data_from_density(
    const float* density,
    uint3 size,
    float voxel_size_meters,
    float solid_threshold) {
    VoxelWorldData data;
    data.chunk_size = kVoxelWorldDataChunkSize;
    data.voxel_size_meters = std::max(voxel_size_meters, 1.0e-6f);

    if (density == nullptr || size.x == 0 || size.y == 0 || size.z == 0) {
        OC_WARNING("build_voxel_world_data_from_density: empty density");
        return data;
    }

    const uint32_t n = data.chunk_size;
    const uint32_t chunks_x = (size.x + n - 1) / n;
    const uint32_t chunks_y = (size.y + n - 1) / n;
    const uint32_t chunks_z = (size.z + n - 1) / n;

    data.chunks.reserve(static_cast<size_t>(chunks_x) * chunks_y * chunks_z / 4);

    for (uint32_t cz = 0; cz < chunks_z; ++cz) {
        for (uint32_t cy = 0; cy < chunks_y; ++cy) {
            for (uint32_t cx = 0; cx < chunks_x; ++cx) {
                ChunkData chunk{};
                chunk.position = make_int3(
                    static_cast<int>(cx),
                    static_cast<int>(cy),
                    static_cast<int>(cz));
                bool any_solid = false;

                for (uint32_t lz = 0; lz < n; ++lz) {
                    const uint32_t wz = cz * n + lz;
                    if (wz >= size.z) {
                        continue;
                    }
                    for (uint32_t ly = 0; ly < n; ++ly) {
                        const uint32_t wy = cy * n + ly;
                        if (wy >= size.y) {
                            continue;
                        }
                        for (uint32_t lx = 0; lx < n; ++lx) {
                            const uint32_t wx = cx * n + lx;
                            if (wx >= size.x) {
                                continue;
                            }
                            const size_t index =
                                (static_cast<size_t>(wz) * size.y + wy) * size.x + wx;
                            if (density[index] > solid_threshold) {
                                chunk.voxels[chunk_data_local_index(n, lx, ly, lz)] = 1;
                                any_solid = true;
                            }
                        }
                    }
                }

                if (any_solid) {
                    data.chunks.push_back(chunk);
                }
            }
        }
    }

    return data;
}

VoxelWorldData build_voxel_world_data_from_noise_texture(
    const NoiseTexture3D& texture,
    float voxel_size_meters,
    float solid_threshold) {
    if (texture.density.size() <
        static_cast<size_t>(texture.size.x) * texture.size.y * texture.size.z) {
        OC_ERROR("build_voxel_world_data_from_noise_texture: density size mismatch");
        return {};
    }
    return build_voxel_world_data_from_density(
        texture.density.data(),
        texture.size,
        voxel_size_meters,
        solid_threshold);
}

}// namespace ocarina
