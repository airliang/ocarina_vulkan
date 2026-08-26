//
// Minimal graphics shader base (Vulkan backend implements this).
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"
#include "pipeline_state.h"

namespace ocarina {

class DescriptorSetLayout;

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

    /// Reflect a named HLSL struct (e.g. MaterialParams) for CPU parameter layout.
    [[nodiscard]] virtual bool get_struct_members(
        const char* struct_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& struct_size) const {
        (void)struct_name;
        members.clear();
        struct_size = 0;
        return false;
    }

    /// True if reflection declares a descriptor resource with this binding name
    /// (UBO, storage buffer, texture, sampler, etc.).
    [[nodiscard]] virtual bool has_descriptor_binding(const char* binding_name) const {
        (void)binding_name;
        return false;
    }

    /// Per-shader descriptor set layouts created after reflection (may be empty).
    [[nodiscard]] virtual const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>&
    descriptor_set_layouts() const {
        static const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> kEmpty{};
        return kEmpty;
    }

    /// Append this shader's reflected push-constant blocks into @p ranges (merged by name).
    virtual void collect_push_constant_ranges(std::vector<PushConstantRange>& ranges) const {
        (void)ranges;
    }

    /// Fill RHI vertex input layout from shader reflection (vertex shaders only).
    [[nodiscard]] virtual bool get_shader_vertex_inputs(
        VertexInputAttributeDescription* out_attributes,
        uint32_t* inout_attribute_count,
        VertexInputBindingDescription* out_bindings,
        uint32_t* inout_binding_count) const {
        (void)out_attributes;
        (void)out_bindings;
        if (inout_attribute_count != nullptr) {
            *inout_attribute_count = 0;
        }
        if (inout_binding_count != nullptr) {
            *inout_binding_count = 0;
        }
        return false;
    }
};

}// namespace ocarina
