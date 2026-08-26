#pragma once

#include "core/stl.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

struct RenderComponent {
    uint32_t mesh_id = InvalidUI32;

    /// Push-constant blocks from material shader reflection (not pipeline layout).
    std::vector<PushConstantRange> push_constants;

    uint32_t material_buffer_offset = InvalidUI32;
    uint32_t material_buffer_size = 0;
};

}// namespace ocarina
