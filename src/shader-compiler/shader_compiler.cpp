#include "shader_compiler.h"

#include "dxc_compiler.h"
#include "core/logging.h"
#include "rhi/context.h"

#include <fstream>

namespace ocarina {

namespace {

std::string get_spv_path_for_shader(const std::string &shader_file_path) {
    return shader_file_path + ".spv";
}

bool load_spirv_from_file(const std::string &spv_path, std::vector<uint32_t> &spirv_code) {
    std::ifstream input(spv_path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamsize file_size = input.tellg();
    if (file_size <= 0 || (file_size % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        return false;
    }

    input.seekg(0, std::ios::beg);
    spirv_code.resize(static_cast<size_t>(file_size / static_cast<std::streamsize>(sizeof(uint32_t))));
    input.read(reinterpret_cast<char *>(spirv_code.data()), file_size);
    return input.good();
}

bool save_spirv_to_file(const std::string &spv_path, const std::vector<uint32_t> &spirv_code) {
    if (spirv_code.empty()) {
        return false;
    }

    std::ofstream output(spv_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output.write(
        reinterpret_cast<const char *>(spirv_code.data()),
        static_cast<std::streamsize>(spirv_code.size() * sizeof(uint32_t)));
    return output.good();
}

bool compile_hlsl_file_to_spirv(
    const std::string &filename,
    ShaderType shader_type,
    const std::string &entry_point,
    std::vector<uint32_t> &spirv_code) {

    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const size_t size = static_cast<size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    if (size == 0) {
        return false;
    }

    std::string hlsl_source(size, '\0');
    input.read(hlsl_source.data(), static_cast<std::streamsize>(size));
    input.close();

    CompileInput compile_input{
        .hlsl = hlsl_source,
        .entry = entry_point,
        .full_file_path = filename,
        .shader_type = shader_type,
        .output_pdbs = false,
    };

    CompileResult compile_result;
    if (!DXCCompiler::compile_hlsl_spriv(compile_input, compile_result)) {
        if (!compile_result.error.empty()) {
            OC_ERROR_FORMAT(
                "Shader compile failed: file='{}' entry='{}' stage={}: {}",
                filename.c_str(),
                entry_point.c_str(),
                static_cast<int>(shader_type),
                compile_result.error.c_str());
        } else {
            OC_ERROR_FORMAT(
                "Shader compile failed: file='{}' entry='{}' stage={}",
                filename.c_str(),
                entry_point.c_str(),
                static_cast<int>(shader_type));
        }
        return false;
    }

    spirv_code = std::move(compile_result.spriv_codes);
    return !spirv_code.empty();
}

} // namespace

bool compile_hlsl_to_spirv_and_reflect(
    const std::string &filename,
    ShaderType shader_type,
    const std::string &entry_point,
    CompiledShader &out) {

    const std::string spv_path = get_spv_path_for_shader(filename);
    out.spirv.clear();
    out.reflection.shader_resources.clear();
    out.reflection.uniform_buffers.clear();
    out.reflection.push_constant_buffers.clear();
    out.reflection.named_structs.clear();
    out.reflection.input_layouts.clear();

    const bool rebuild = RHIContext::instance().rebuild_shaders();
    const bool loaded_from_cache = !rebuild && load_spirv_from_file(spv_path, out.spirv);

    if (!loaded_from_cache) {
        if (rebuild) {
            OC_INFO_FORMAT("rebuildshader: compiling {} (ignoring {})", filename.c_str(), spv_path.c_str());
        }

        if (!compile_hlsl_file_to_spirv(filename, shader_type, entry_point, out.spirv)) {
            return false;
        }

        if (!save_spirv_to_file(spv_path, out.spirv)) {
            OC_ERROR_FORMAT("Failed to write SPIR-V cache file: {}", spv_path.c_str());
        }
    }

    if (out.spirv.empty()) {
        return false;
    }

    DXCCompiler::run_spriv_reflection(out.spirv, shader_type, out.reflection);
    return true;
}

} // namespace ocarina

