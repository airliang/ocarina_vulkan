#include "shader_compiler.h"

#include "dxc_compiler.h"
#include "core/hash.h"
#include "core/logging.h"
#include "rhi/shader_program_key.h"

#include <fstream>
#include <filesystem>
#include <mutex>

namespace ocarina {

namespace {

/// Serializes HLSL→SPIR-V compile + disk cache. PipelineCompileTasks run in parallel and
/// often share a stage (e.g. mesh.vert for opaque + ALPHA_BLEND variants).
std::mutex& shader_compile_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::string directory_of(const std::string &shader_file_path) {
    const size_t slash = shader_file_path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    return shader_file_path.substr(0, slash + 1);
}

/// SPIR-V cache file: <shader_dir>/<16-hex-hash>.spv (not the source filename).
std::string get_spv_path_for_shader_hash(
    const std::string &shader_file_path,
    uint64_t shader_hash) {
    return directory_of(shader_file_path) + shader_stage_hash_hex(shader_hash) + ".spv";
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
    output.flush();
    return output.good();
}

/// Write via a temp file then rename so readers never see a partial .spv.
bool save_spirv_to_file_atomic(
    const std::string &spv_path,
    const std::vector<uint32_t> &spirv_code) {
    const std::string tmp_path = spv_path + ".tmp";
    if (!save_spirv_to_file(tmp_path, spirv_code)) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(spv_path, ec);
    std::filesystem::rename(tmp_path, spv_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

bool compile_hlsl_file_to_spirv(
    const std::string &filename,
    ShaderType shader_type,
    const std::string &entry_point,
    const std::set<std::string> &options,
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
        .macros = std::vector<std::string>(options.begin(), options.end()),
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
    CompiledShader &out,
    bool rebuild_shaders,
    const std::set<std::string> &options) {

    // Guard compile + SPV cache: parallel PipelineCompileTasks share stages
    // (same hash → same .spv path) and DXC is not safe for concurrent use.
    std::lock_guard<std::mutex> lock(shader_compile_mutex());

    out.spirv.clear();
    out.reflection.shader_resources.clear();
    out.reflection.uniform_buffers.clear();
    out.reflection.push_constant_buffers.clear();
    out.reflection.named_structs.clear();
    out.reflection.input_layouts.clear();
    out.shader_hash = compute_shader_stage_hash(filename, shader_type, entry_point, options);

    const std::string spv_path = get_spv_path_for_shader_hash(filename, out.shader_hash);
    const std::string hash_hex = shader_stage_hash_hex(out.shader_hash);

    // Re-check cache under the lock (another thread may have just written it).
    const bool loaded_from_cache = !rebuild_shaders && load_spirv_from_file(spv_path, out.spirv);

    if (!loaded_from_cache) {
        if (rebuild_shaders) {
            OC_INFO_FORMAT(
                "rebuildshader: compiling {} hash={} (ignoring {})",
                filename.c_str(),
                hash_hex.c_str(),
                spv_path.c_str());
        }

        if (!compile_hlsl_file_to_spirv(filename, shader_type, entry_point, options, out.spirv)) {
            out.shader_hash = 0;
            return false;
        }

        if (!save_spirv_to_file_atomic(spv_path, out.spirv)) {
            OC_ERROR_FORMAT("Failed to write SPIR-V cache file: {}", spv_path.c_str());
        } else {
            OC_INFO_FORMAT(
                "Wrote SPIR-V cache {} for {} (stage={}, options={})",
                spv_path.c_str(),
                filename.c_str(),
                static_cast<int>(shader_type),
                static_cast<int>(options.size()));
        }
    }

    if (out.spirv.empty()) {
        out.shader_hash = 0;
        return false;
    }

    DXCCompiler::run_spriv_reflection(out.spirv, shader_type, out.reflection);
    return true;
}

} // namespace ocarina
