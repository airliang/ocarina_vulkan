#pragma once

#include "core/stl.h"

namespace ocarina {

struct RenderComponent {
    uint32_t mesh_id = InvalidUI32;

    std::byte* push_constant_data = nullptr;
    uint8_t push_constant_size = 0;

    uint32_t material_buffer_offset = InvalidUI32;
    uint32_t material_buffer_size = 0;
};

}// namespace ocarina
