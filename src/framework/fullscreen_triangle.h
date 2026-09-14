#pragma once

#include "rhi/command_buffer.h"

namespace ocarina {

/// Draws a screen-covering triangle with no vertex/index buffers (SV_VertexID).
class FullscreenTriangle {
public:
    static void draw(CommandBuffer &cmd) {
        cmd.draw(3, 1, 0, 0);
    }
};

}// namespace ocarina
