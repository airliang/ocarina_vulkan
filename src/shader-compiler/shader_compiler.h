#pragma once

#include <string>
#include <vector>

#include "rhi/graphics_descriptions.h"
#include "shader_reflection.h"

namespace ocarina {

struct CompiledShader {
    std::vector<uint32_t> spirv;
    ShaderReflection reflection;
};

// Compile HLSL into SPIR-V and generate backend-usable reflection metadata.
// Shader cache location is handled internally (filename + ".spv").
bool compile_hlsl_to_spirv_and_reflect(
    const std::string &filename,
    ShaderType shader_type,
    const std::string &entry_point,
    CompiledShader &out);

} // namespace ocarina

