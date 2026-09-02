//
// Minimal graphics shader base (Vulkan backend implements this).
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"
#include "pipeline_state.h"
#include "shader_program.h"

namespace ocarina {

class DescriptorSetLayout;
class ShaderProgram;

/// GPU-side shader module (e.g. VkShaderModule). Reflection lives on the parent ShaderProgram.
class RHIShader {
public:
    struct UniformBufferMember {
        string name;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    explicit RHIShader(ShaderProgram* program = nullptr, ShaderType stage = ShaderType::VertexShader)
        : program_(program), stage_(stage) {}
    virtual ~RHIShader() = default;

    [[nodiscard]] ShaderProgram* program() const noexcept { return program_; }
    [[nodiscard]] ShaderType stage() const noexcept { return stage_; }
    void set_program(ShaderProgram* program) noexcept { program_ = program; }

    [[nodiscard]] virtual bool get_uniform_buffer_members(
        const char* buffer_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& buffer_size) const {
        if (program_ != nullptr) {
            std::vector<ShaderProgram::UniformBufferMember> program_members;
            if (program_->get_uniform_buffer_members(buffer_name, program_members, buffer_size)) {
                members.clear();
                members.reserve(program_members.size());
                for (const ShaderProgram::UniformBufferMember& member : program_members) {
                    UniformBufferMember out;
                    out.name = member.name;
                    out.type = member.type;
                    out.size = member.size;
                    out.offset = member.offset;
                    members.push_back(std::move(out));
                }
                return true;
            }
        }
        (void)buffer_name;
        members.clear();
        buffer_size = 0;
        return false;
    }

    [[nodiscard]] virtual bool get_struct_members(
        const char* struct_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& struct_size) const {
        if (program_ != nullptr) {
            std::vector<ShaderProgram::UniformBufferMember> program_members;
            if (program_->get_struct_members(struct_name, program_members, struct_size)) {
                members.clear();
                members.reserve(program_members.size());
                for (const ShaderProgram::UniformBufferMember& member : program_members) {
                    UniformBufferMember out;
                    out.name = member.name;
                    out.type = member.type;
                    out.size = member.size;
                    out.offset = member.offset;
                    members.push_back(std::move(out));
                }
                return true;
            }
        }
        (void)struct_name;
        members.clear();
        struct_size = 0;
        return false;
    }

    [[nodiscard]] virtual bool has_descriptor_binding(const char* binding_name) const {
        return program_ != nullptr && program_->has_descriptor_binding(binding_name);
    }

    [[nodiscard]] virtual const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>&
    descriptor_set_layouts() const {
        if (program_ != nullptr) {
            return program_->descriptor_set_layouts();
        }
        static const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> kEmpty{};
        return kEmpty;
    }

    virtual void collect_push_constant_ranges(std::vector<PushConstantRange>& ranges) const {
        if (program_ != nullptr) {
            program_->collect_push_constant_ranges(ranges);
        } else {
            (void)ranges;
        }
    }

    [[nodiscard]] virtual bool get_shader_vertex_inputs(
        VertexInputAttributeDescription* out_attributes,
        uint32_t* inout_attribute_count,
        VertexInputBindingDescription* out_bindings,
        uint32_t* inout_binding_count) const {
        if (program_ != nullptr) {
            return program_->get_shader_vertex_inputs(
                out_attributes,
                inout_attribute_count,
                out_bindings,
                inout_binding_count);
        }
        if (inout_attribute_count != nullptr) {
            *inout_attribute_count = 0;
        }
        if (inout_binding_count != nullptr) {
            *inout_binding_count = 0;
        }
        return false;
    }

private:
    ShaderProgram* program_ = nullptr;
    ShaderType stage_ = ShaderType::VertexShader;
};

}// namespace ocarina
