#pragma once

#include "core/header.h"

namespace ocarina {

struct MeshGeometrySlice {
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
};

[[nodiscard]] inline bool is_valid_geometry_slice(const MeshGeometrySlice& slice) {
    return slice.vertex_count > 0 && slice.index_count > 0;
}

}// namespace ocarina
