#include "chunk.h"

namespace ocarina {

VoxelMaterialId Chunk::material_at(uint32_t chunk_size, uint3 local) const noexcept {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return kVoxelAir;
    }
    const uint32_t packed = pack_voxel_local(chunk_size, local);
    for (const Voxel& voxel : voxels) {
        if (voxel.position == packed) {
            return voxel.material;
        }
    }
    return kVoxelAir;
}

float3 Chunk::color_at(uint32_t chunk_size, uint3 local) const noexcept {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return make_float3(1.0f);
    }
    const uint32_t packed = pack_voxel_local(chunk_size, local);
    for (const Voxel& voxel : voxels) {
        if (voxel.position == packed) {
            return voxel.color;
        }
    }
    return make_float3(1.0f);
}

bool Chunk::set_voxel(
    uint32_t chunk_size,
    uint3 local,
    VoxelMaterialId material,
    const float3& color) {
    if (!is_valid_voxel_local(chunk_size, local)) {
        return false;
    }

    const uint32_t packed = pack_voxel_local(chunk_size, local);
    for (size_t index = 0; index < voxels.size(); ++index) {
        if (voxels[index].position != packed) {
            continue;
        }
        if (material == kVoxelAir) {
            voxels[index] = voxels.back();
            voxels.pop_back();
            return true;
        }

        Voxel& voxel = voxels[index];
        const uint32_t samples = std::max(voxel.color_samples, 1u);
        const float3 sum = voxel.color * static_cast<float>(samples) + color;
        const uint32_t next_samples = samples + 1u;
        voxel.color = sum / static_cast<float>(next_samples);
        voxel.color_samples = next_samples;
        voxel.material = material;
        return true;
    }

    if (material == kVoxelAir) {
        return false;
    }

    Voxel voxel{};
    voxel.chunk_id = id;
    voxel.position = packed;
    voxel.material = material;
    voxel.color = color;
    voxel.color_samples = 1;
    voxels.push_back(voxel);
    return true;
}

}// namespace ocarina
