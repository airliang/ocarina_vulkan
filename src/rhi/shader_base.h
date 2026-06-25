//
// Minimal graphics shader base (Vulkan backend implements this).
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {

class RHIShader {
public:
    struct UniformBufferMember {
        string name;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    virtual ~RHIShader() = default;

    [[nodiscard]] virtual bool get_uniform_buffer_members(
        const char* buffer_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& buffer_size) const {
        (void)buffer_name;
        members.clear();
        buffer_size = 0;
        return false;
    }
};

}// namespace ocarina
