#pragma once

#include "core/stl.h"
#include "mesh_geometry.h"

namespace ocarina {

class DescriptorSet;

struct RenderComponent {
    MeshGeometrySlice geometry{};

    std::vector<DescriptorSet*> descriptor_sets;
    uint32_t first_set = 0;

    std::byte* push_constant_data = nullptr;
    uint8_t push_constant_size = 0;
};

}// namespace ocarina
