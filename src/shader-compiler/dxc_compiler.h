//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/shader_reflection.h"
#include "spirv_cross.hpp"

namespace ocarina {

struct CompileInput {
    ocarina_string hlsl;
    ocarina_string entry;
    ocarina_string full_file_path;
    ocarina_vector<ocarina_string> macros;
    ocarina_vector<ocarina_string> include_paths;
    ShaderType shader_type;
    bool output_pdbs;
};

struct CompileResult {
    ocarina_vector<uint32_t> spriv_codes;
    ocarina_string error;
};

class DXCCompiler : public concepts::Noncopyable {
public:
    static bool compile_hlsl_spriv(const CompileInput& input, CompileResult& result);
    static void run_spriv_reflection(
        const ocarina_vector<uint32_t>& spriv,
        ShaderType shader_type,
        ShaderReflection& shader_reflection);

    DXCCompiler() = default;
    DXCCompiler(const DXCCompiler&) = delete;
    DXCCompiler(DXCCompiler&&) = delete;
    DXCCompiler operator=(const DXCCompiler&) = delete;
    DXCCompiler operator=(DXCCompiler&&) = delete;
    ~DXCCompiler() {}

private:
    static ShaderVariableType get_shader_variable_type(
        uint32_t vec_size,
        uint32_t column_size,
        const spirv_cross::SPIRType& type);
};

} // namespace ocarina
