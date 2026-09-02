#include "shader_program.h"

#include "context.h"
#include "core/hash.h"
#include "core/logging.h"
#include "device.h"
#include "shader_compiler.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace ocarina {

namespace {

uint32_t shader_stage_flags_for_type(ShaderType shader_type) {
    switch (shader_type) {
        case ShaderType::VertexShader:
            return 1u; // VK_SHADER_STAGE_VERTEX_BIT
        case ShaderType::PixelShader:
            return 16u; // VK_SHADER_STAGE_FRAGMENT_BIT
        case ShaderType::GeometryShader:
            return 4u;
        case ShaderType::ComputeShader:
            return 32u;
        case ShaderType::MeshShader:
            return 0x00000080u;
        default:
            return 1u;
    }
}

uint32_t vertex_format_stride(VertexFormat format) {
    switch (format) {
        case VertexFormat::R32_SFLOAT:
            return 4;
        case VertexFormat::R32G32_SFLOAT:
            return 8;
        case VertexFormat::R32G32B32_SFLOAT:
            return 12;
        case VertexFormat::R32G32B32A32_SFLOAT:
            return 16;
        case VertexFormat::R32_SINT:
            return 4;
        case VertexFormat::R32G32_SINT:
            return 8;
        case VertexFormat::R32G32B32_SINT:
            return 12;
        case VertexFormat::R32G32B32A32_SINT:
            return 16;
        case VertexFormat::R32_UINT:
            return 4;
        case VertexFormat::R32G32_UINT:
            return 8;
        case VertexFormat::R32G32B32_UINT:
            return 12;
        case VertexFormat::R32G32B32A32_UINT:
            return 16;
        case VertexFormat::R8G8B8A8_UNORM:
            return 4;
        default:
            return 0;
    }
}

uint32_t make_binding_key(uint8_t descriptor_set, uint8_t binding) {
    return (static_cast<uint32_t>(descriptor_set) << 16) | static_cast<uint32_t>(binding);
}

ShaderBindingType binding_type_from_resource(const ShaderReflection::ShaderResource& shader_resource) {
    if (shader_resource.parameter_type == ShaderReflection::ResourceType::ConstantBuffer) {
        return ShaderBindingType::UniformBuffer;
    }
    if (shader_resource.parameter_type == ShaderReflection::ResourceType::SRV) {
        return (shader_resource.is_bindless || shader_resource.is_separate_image)
            ? ShaderBindingType::SampledImage
            : ShaderBindingType::CombinedImageSampler;
    }
    if (shader_resource.parameter_type == ShaderReflection::ResourceType::UAV) {
        return ShaderBindingType::StorageImage;
    }
    if (shader_resource.parameter_type == ShaderReflection::ResourceType::Sampler) {
        return ShaderBindingType::Sampler;
    }
    if (shader_resource.parameter_type == ShaderReflection::ResourceType::StorageBuffer) {
        return ShaderBindingType::StorageBuffer;
    }
    return ShaderBindingType::UniformBuffer;
}

} // namespace

handle_ty ShaderProgram::shader_handle(ShaderType stage) const noexcept {
    switch (stage) {
        case ShaderType::VertexShader:
            return reinterpret_cast<handle_ty>(vertex_shader_);
        case ShaderType::PixelShader:
            return reinterpret_cast<handle_ty>(pixel_shader_);
        case ShaderType::ComputeShader:
            return reinterpret_cast<handle_ty>(compute_shader_);
        default:
            return 0;
    }
}

void ShaderProgram::ensure_gpu_shaders(Device* device) {
    if (device == nullptr) {
        return;
    }
    if (is_graphics()) {
        if (vertex_shader_ == nullptr) {
            vertex_shader_ = reinterpret_cast<RHIShader*>(
                device->create_shader_from_program(this, ShaderType::VertexShader));
        }
        if (pixel_shader_ == nullptr) {
            pixel_shader_ = reinterpret_cast<RHIShader*>(
                device->create_shader_from_program(this, ShaderType::PixelShader));
        }
    } else if (is_compute() && compute_shader_ == nullptr) {
        compute_shader_ = reinterpret_cast<RHIShader*>(
            device->create_shader_from_program(this, ShaderType::ComputeShader));
    }
}

ShaderProgram* ShaderProgram::compile_graphics_from_HLSL(
    const std::string& vertex_shader_file,
    const std::string& pixel_shader_file,
    const std::set<std::string>& vertex_options,
    const std::set<std::string>& pixel_options,
    const std::string& entry_point) {

    CompiledShader vertex_compiled{};
    if (!compile_hlsl_to_spirv_and_reflect(
            vertex_shader_file,
            ShaderType::VertexShader,
            entry_point,
            vertex_compiled,
            RHIContext::instance().rebuild_shaders())) {
        return nullptr;
    }

    CompiledShader pixel_compiled{};
    if (!compile_hlsl_to_spirv_and_reflect(
            pixel_shader_file,
            ShaderType::PixelShader,
            entry_point,
            pixel_compiled,
            RHIContext::instance().rebuild_shaders())) {
        return nullptr;
    }

    auto* program = ocarina::new_with_allocator<ShaderProgram>();
    program->key_.vertex_shader_file = vertex_shader_file;
    program->key_.pixel_shader_file = pixel_shader_file;
    program->key_.vertex_options = vertex_options;
    program->key_.pixel_options = pixel_options;
    program->key_.entry_point = entry_point;
    program->vertex_spirv_ = std::move(vertex_compiled.spirv);
    program->pixel_spirv_ = std::move(pixel_compiled.spirv);

    program->merge_stage_reflection(
        vertex_compiled.reflection,
        ShaderType::VertexShader,
        shader_stage_flags_for_type(ShaderType::VertexShader));
    program->merge_stage_reflection(
        pixel_compiled.reflection,
        ShaderType::PixelShader,
        shader_stage_flags_for_type(ShaderType::PixelShader));
    program->build_vertex_attributes(vertex_compiled.reflection);
    program->finalize_merged_reflection();
    return program;
}

ShaderProgram* ShaderProgram::compile_compute_from_HLSL(
    const std::string& compute_shader_file,
    const std::set<std::string>& options,
    const std::string& entry_point) {

    CompiledShader compiled{};
    if (!compile_hlsl_to_spirv_and_reflect(
            compute_shader_file,
            ShaderType::ComputeShader,
            entry_point,
            compiled,
            RHIContext::instance().rebuild_shaders())) {
        return nullptr;
    }

    auto* program = ocarina::new_with_allocator<ShaderProgram>();
    program->key_.compute_shader_file = compute_shader_file;
    program->key_.compute_options = options;
    program->key_.entry_point = entry_point;
    program->compute_spirv_ = std::move(compiled.spirv);
    program->merge_stage_reflection(
        compiled.reflection,
        ShaderType::ComputeShader,
        shader_stage_flags_for_type(ShaderType::ComputeShader));
    program->finalize_merged_reflection();
    return program;
}

void ShaderProgram::create_descriptor_set_layouts(Device* device) {
    if (device == nullptr || descriptor_set_layouts_ready_) {
        return;
    }
    create_descriptor_set_layouts(device->create_shader_descriptor_set_layouts(this));
}

void ShaderProgram::create_descriptor_set_layouts(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    if (descriptor_set_layouts_ready_) {
        return;
    }
    descriptor_set_layouts_ = layouts;
    descriptor_set_layouts_ready_ = true;
}

void ShaderProgram::merge_stage_reflection(
    const ShaderReflection& reflection,
    ShaderType shader_type,
    uint32_t stage_flags) {
    (void)shader_type;

    std::unordered_map<uint32_t, size_t> binding_index_by_key;
    binding_index_by_key.reserve(variables_.size() + reflection.shader_resources.size());

    for (size_t i = 0; i < variables_.size(); ++i) {
        const ShaderVariableBinding& existing = variables_[i];
        binding_index_by_key.emplace(
            make_binding_key(existing.descriptor_set, existing.binding),
            i);
    }

    auto merge_or_add_binding = [&](ShaderVariableBinding variable) {
        const uint32_t key = make_binding_key(variable.descriptor_set, variable.binding);
        const auto it = binding_index_by_key.find(key);
        if (it == binding_index_by_key.end()) {
            binding_index_by_key.emplace(key, variables_.size());
            variables_.push_back(std::move(variable));
            return;
        }

        ShaderVariableBinding& existing = variables_[it->second];
        existing.stage_flags |= variable.stage_flags;
        if (variable.size > existing.size) {
            existing.size = variable.size;
        }
        if (existing.members.empty() && !variable.members.empty()) {
            existing.members = std::move(variable.members);
        }
    };

    for (const ShaderReflection::ShaderResource& shader_resource : reflection.shader_resources) {
        ShaderVariableBinding variable{};
        std::strncpy(variable.name, shader_resource.name.c_str(), sizeof(variable.name) - 1);
        variable.binding = static_cast<uint8_t>(shader_resource.binding);
        variable.descriptor_set = static_cast<uint8_t>(shader_resource.descriptor_set);
        variable.size = shader_resource.size;
        variable.count = 1;
        variable.is_bindless = shader_resource.is_bindless;
        variable.stage_flags = stage_flags;
        variable.type = binding_type_from_resource(shader_resource);
        merge_or_add_binding(std::move(variable));
    }

    for (const ShaderReflection::UniformBuffer& ubo : reflection.uniform_buffers) {
        ShaderVariableBinding variable{};
        std::strncpy(variable.name, ubo.name.c_str(), sizeof(variable.name) - 1);
        variable.binding = ubo.binding;
        variable.descriptor_set = ubo.descriptor_set;
        variable.size = ubo.size;
        variable.count = 1;
        variable.type = ShaderBindingType::UniformBuffer;
        variable.stage_flags = stage_flags;
        variable.members = ubo.shader_variables;
        variable.is_bindless = ubo.is_bindless;
        merge_or_add_binding(std::move(variable));
    }

    for (const ShaderReflection::UniformBuffer& push_constant : reflection.push_constant_buffers) {
        bool found = false;
        for (ShaderPushConstant& existing : push_constants_) {
            if (existing.name == push_constant.name
                && existing.offset == push_constant.offset
                && existing.size == push_constant.size) {
                existing.stage_flags |= stage_flags;
                if (existing.shader_variables.empty() && !push_constant.shader_variables.empty()) {
                    existing.shader_variables = push_constant.shader_variables;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            ShaderPushConstant pc;
            pc.offset = push_constant.offset;
            pc.size = push_constant.size;
            pc.shader_variables = push_constant.shader_variables;
            pc.name = push_constant.name;
            pc.stage_flags = stage_flags;
            push_constants_.push_back(std::move(pc));
        }
    }

    for (const ShaderReflection::UniformBuffer& named_struct : reflection.named_structs) {
        bool found = false;
        for (const ShaderReflection::UniformBuffer& existing : named_structs_) {
            if (existing.name == named_struct.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            named_structs_.push_back(named_struct);
        }
    }
}

void ShaderProgram::finalize_merged_reflection() {
    std::sort(variables_.begin(), variables_.end(), [](const ShaderVariableBinding& lhs,
                                                        const ShaderVariableBinding& rhs) {
        if (lhs.descriptor_set != rhs.descriptor_set) {
            return lhs.descriptor_set < rhs.descriptor_set;
        }
        return lhs.binding < rhs.binding;
    });
}

void ShaderProgram::build_vertex_attributes(const ShaderReflection& reflection) {
    vertex_attributes_.clear();
    for (const ShaderReflection::ShaderResource& shader_resource : reflection.input_layouts) {
        if (shader_resource.parameter_type != ShaderReflection::ResourceType::InputAttachment) {
            continue;
        }
        VertexAttribute attrib{};
        attrib.binding = static_cast<uint8_t>(shader_resource.register_);
        attrib.location = static_cast<uint8_t>(shader_resource.location);
        attrib.offset = static_cast<uint8_t>(shader_resource.offset);
        attrib.format = static_cast<uint32_t>(shader_resource.format);
        attrib.type = static_cast<uint8_t>(shader_resource.vertex_attribute_type);
        vertex_attributes_.push_back(attrib);
    }
}

bool ShaderProgram::get_uniform_buffer_members(
    const char* buffer_name,
    std::vector<UniformBufferMember>& members,
    uint32_t& buffer_size) const {
    members.clear();
    buffer_size = 0;
    if (buffer_name == nullptr) {
        return false;
    }

    for (const ShaderVariableBinding& binding : variables_) {
        if (binding.type != ShaderBindingType::UniformBuffer) {
            continue;
        }
        if (std::strcmp(binding.name, buffer_name) != 0) {
            continue;
        }

        buffer_size = binding.size;
        members.reserve(binding.members.size());
        for (const ShaderReflection::ShaderVariable& variable : binding.members) {
            UniformBufferMember member;
            member.name = variable.name;
            member.type = variable.variable_type;
            member.size = variable.size;
            member.offset = variable.offset;
            members.push_back(std::move(member));
        }
        return true;
    }
    return false;
}

bool ShaderProgram::get_struct_members(
    const char* struct_name,
    std::vector<UniformBufferMember>& members,
    uint32_t& struct_size) const {
    members.clear();
    struct_size = 0;
    if (struct_name == nullptr) {
        return false;
    }

    for (const ShaderReflection::UniformBuffer& named_struct : named_structs_) {
        if (named_struct.name != struct_name) {
            continue;
        }
        struct_size = named_struct.size;
        members.reserve(named_struct.shader_variables.size());
        for (const ShaderReflection::ShaderVariable& variable : named_struct.shader_variables) {
            UniformBufferMember member;
            member.name = variable.name;
            member.type = variable.variable_type;
            member.size = variable.size;
            member.offset = variable.offset;
            members.push_back(std::move(member));
        }
        return !members.empty() && struct_size > 0;
    }
    return false;
}

bool ShaderProgram::has_descriptor_binding(const char* binding_name) const {
    if (binding_name == nullptr || binding_name[0] == '\0') {
        return false;
    }
    for (const ShaderVariableBinding& binding : variables_) {
        if (std::strcmp(binding.name, binding_name) == 0) {
            return true;
        }
    }
    return false;
}

void ShaderProgram::collect_push_constant_ranges(std::vector<PushConstantRange>& ranges) const {
    for (const ShaderPushConstant& pc : push_constants_) {
        if (pc.size == 0) {
            continue;
        }

        PushConstantRange* existing = nullptr;
        for (PushConstantRange& range : ranges) {
            if (range.name == pc.name && range.offset == static_cast<uint16_t>(pc.offset)
                && range.size
                    == static_cast<uint16_t>(std::min<uint32_t>(pc.size, PushConstantRange::kMaxDataBytes))) {
                existing = &range;
                break;
            }
        }

        if (existing == nullptr) {
            PushConstantRange range;
            range.name = pc.name;
            range.offset = static_cast<uint16_t>(pc.offset);
            range.size = static_cast<uint16_t>(
                std::min<uint32_t>(pc.size, static_cast<uint32_t>(PushConstantRange::kMaxDataBytes)));
            range.shader_stage = static_cast<uint8_t>(pc.stage_flags & 0xFFu);
            range.data.fill(std::byte{0});
            for (const ShaderReflection::ShaderVariable& variable : pc.shader_variables) {
                PushConstantVariable pc_variable;
                pc_variable.offset = variable.offset;
                pc_variable.size = variable.size;
                range.variables.emplace(hash64(variable.name), pc_variable);
            }
            ranges.push_back(std::move(range));
        } else {
            existing->shader_stage =
                static_cast<uint8_t>((static_cast<uint32_t>(existing->shader_stage) | pc.stage_flags) & 0xFFu);
            for (const ShaderReflection::ShaderVariable& variable : pc.shader_variables) {
                PushConstantVariable pc_variable;
                pc_variable.offset = variable.offset;
                pc_variable.size = variable.size;
                existing->variables.emplace(hash64(variable.name), pc_variable);
            }
        }
    }
}

bool ShaderProgram::get_shader_vertex_inputs(
    VertexInputAttributeDescription* out_attributes,
    uint32_t* inout_attribute_count,
    VertexInputBindingDescription* out_bindings,
    uint32_t* inout_binding_count) const {
    if (out_attributes == nullptr || inout_attribute_count == nullptr || out_bindings == nullptr
        || inout_binding_count == nullptr) {
        return false;
    }

    if (vertex_attributes_.empty()) {
        *inout_attribute_count = 0;
        *inout_binding_count = 0;
        return true;
    }

    std::vector<size_t> attribute_order(vertex_attributes_.size());
    std::iota(attribute_order.begin(), attribute_order.end(), size_t{0});
    std::sort(attribute_order.begin(), attribute_order.end(), [this](size_t lhs, size_t rhs) {
        return vertex_attributes_[lhs].location < vertex_attributes_[rhs].location;
    });

    const uint32_t attr_count = static_cast<uint32_t>(vertex_attributes_.size());
    if (attr_count > *inout_attribute_count || attr_count > *inout_binding_count) {
        return false;
    }

    for (uint32_t i = 0; i < attr_count; ++i) {
        const VertexAttribute& attr = vertex_attributes_[attribute_order[i]];
        out_attributes[i].location = attr.location;
        out_attributes[i].binding = static_cast<uint8_t>(i);
        out_attributes[i].format = static_cast<VertexFormat>(attr.format);
        out_attributes[i].offset = 0;
        out_bindings[i].binding = static_cast<uint16_t>(i);
        out_bindings[i].stride = vertex_format_stride(static_cast<VertexFormat>(attr.format));
        out_bindings[i].input_rate = VertexInputRate::Vertex;
    }

    *inout_attribute_count = attr_count;
    *inout_binding_count = attr_count;
    return true;
}

} // namespace ocarina
