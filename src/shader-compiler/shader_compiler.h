#pragma once

#include <string>
#include <vector>
#include <set>

#include "rhi/graphics_descriptions.h"
#include "rhi/shader_reflection.h"

namespace ocarina {

struct CompiledShader {
    ocarina_vector<uint32_t> spirv;
    ShaderReflection reflection;
    /// Identity of this stage compile (filename + type + entry + options).
    uint64_t shader_hash = 0;
};

// Compile HLSL into SPIR-V and generate backend-usable reflection metadata.
// @p options become DXC -D macros (e.g. "ALPHA_BLEND=1").
// SPIR-V is cached as <shader_dir>/<hex(shader_hash)>.spv.
// When @p output_pdbs is true, compile with RenderDoc debug info embedded in the SPIR-V
// (-fspv-debug=vulkan-with-source); DXC does not emit a separate .pdb for -spirv.
bool compile_hlsl_to_spirv_and_reflect(
    const ocarina_string &filename,
    ShaderType shader_type,
    const ocarina_string &entry_point,
    CompiledShader &out,
    bool rebuild_shaders = false,
    const ocarina_set<ocarina_string> &options = {},
    bool output_pdbs = false);

} // namespace ocarina

