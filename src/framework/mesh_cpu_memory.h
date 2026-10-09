#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/vertex_buffer.h"

namespace ocarina {

inline constexpr const char* kMeshCpuPositionsPool = "MeshCPU.positions";
inline constexpr const char* kMeshCpuNormalsPool = "MeshCPU.normals";
inline constexpr const char* kMeshCpuTangentsPool = "MeshCPU.tangents";
inline constexpr const char* kMeshCpuUvsPool = "MeshCPU.uvs";
inline constexpr const char* kMeshCpuColorsPool = "MeshCPU.colors";
inline constexpr const char* kMeshCpuIndicesPool = "MeshCPU.indices";

using MeshPositions = ocarina_vector<Vector3>;
using MeshNormals = ocarina_vector<Vector3>;
using MeshTangents = ocarina_vector<Vector4>;
using MeshUvs = ocarina_vector<Vector2>;
using MeshColors = ocarina_vector<Vector4>;
using MeshIndices = ocarina_vector<uint16_t>;

inline MeshPositions make_mesh_positions() {
    return make_ocarina_vector<Vector3>(kMeshCpuPositionsPool);
}
inline MeshNormals make_mesh_normals() {
    return make_ocarina_vector<Vector3>(kMeshCpuNormalsPool);
}
inline MeshTangents make_mesh_tangents() {
    return make_ocarina_vector<Vector4>(kMeshCpuTangentsPool);
}
inline MeshUvs make_mesh_uvs() {
    return make_ocarina_vector<Vector2>(kMeshCpuUvsPool);
}
inline MeshColors make_mesh_colors() {
    return make_ocarina_vector<Vector4>(kMeshCpuColorsPool);
}
inline MeshIndices make_mesh_indices() {
    return make_ocarina_vector<uint16_t>(kMeshCpuIndicesPool);
}

}// namespace ocarina
