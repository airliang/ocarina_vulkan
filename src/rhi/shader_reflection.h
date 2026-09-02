#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {

/// CPU-side reflection metadata produced from SPIR-V (HLSL) shaders.
struct ShaderReflection {
    enum ResourceType {
        ConstantBuffer,
        Texture,
        SRV,
        UAV,
        Sampler,
        InputAttachment,
        Atomic,
        StorageBuffer,
    };

    int thread_group_size[3] = {0, 0, 0};

    struct ShaderResource {
        uint32_t shader_type : 4 = 0;
        uint32_t register_ : 4 = 0;
        uint32_t register_count : 4 = 0;
        uint32_t location : 4 = 0;
        uint32_t offset : 5 = 0;
        uint32_t parameter_type : 3 = 0;
        uint32_t binding : 4 = 0;
        uint32_t descriptor_set : 4 = 0;
        uint32_t size = 0;
        uint32_t array_size = 0;
        bool is_bindless = false;
        /// HLSL Texture2D + separate SamplerState: needs SAMPLED_IMAGE, not COMBINED_IMAGE_SAMPLER.
        bool is_separate_image = false;
        VertexFormat format = VertexFormat::Undefined;
        VertexAttributeType::Enum vertex_attribute_type = VertexAttributeType::Enum::Count;
        std::string name;
    };

    struct ShaderVariable {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t register_ = 0;
        uint32_t register_count = 0;
        uint32_t descriptor_set = 0;
        uint32_t binding_ = 0;
        ShaderVariableType variable_type = ShaderVariableType::FLOAT;

        bool operator==(const ShaderVariable& other) const {
            return offset == other.offset && register_ == other.register_
                && register_count == other.register_count && descriptor_set == other.descriptor_set
                && variable_type == other.variable_type && binding_ == other.binding_
                && name == other.name && size == other.size;
        }

        bool operator!=(const ShaderVariable& other) const { return !(*this == other); }
    };

    struct UniformBuffer {
        std::string name;
        uint8_t binding = 0;
        uint8_t descriptor_set = 0;
        uint32_t size = 0;
        uint32_t offset = 0;
        bool is_bindless = false;
        std::vector<ShaderVariable> shader_variables;
    };

    std::vector<ShaderResource> shader_resources;
    std::vector<UniformBuffer> uniform_buffers;
    std::vector<UniformBuffer> push_constant_buffers;
    /// Named OpTypeStruct layouts (e.g. per-shader MaterialParams) for CPU mirrors.
    std::vector<UniformBuffer> named_structs;
    std::vector<ShaderResource> input_layouts;
};

} // namespace ocarina
