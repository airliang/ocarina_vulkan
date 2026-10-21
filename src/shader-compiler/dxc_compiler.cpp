//
// Created by Zero on 06/08/2022.
//

#include "dxc_compiler.h"
#include "core/logging.h"
#include <cstring>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include "dxcapi.h"
#include <wrl/client.h>

using namespace Microsoft::WRL;
#endif

namespace ocarina {

class CustomIncludeHandler : public IDxcIncludeHandler {
public:
    CustomIncludeHandler(IDxcUtils* utils) 
    { 
        pUtils = utils; 
    }

    HRESULT try_load_file(const char *file_full_path, IDxcBlobEncoding** output) {
        std::ifstream is(file_full_path);
        if (is.is_open()) {
            is.seekg(0, std::ios::end);
            size_t size = is.tellg();
            is.seekg(0, std::ios::beg);
            char *shaderCode = new char[size + 1];
            shaderCode[size] = '\0';
            memset(shaderCode, 0, size + 1);
            is.read(shaderCode, size + 1);
            is.close();
            ocarina_string shader_code(shaderCode);
            HRESULT hr = pUtils->CreateBlob(shader_code.data(), shader_code.size(), 0, output);
            
            delete[] shaderCode;
            return hr;
        }

        return E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE 
        LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob **ppIncludeSource) override {
        ComPtr<IDxcBlobEncoding> pEncoding;
        ocarina_string file_name = wstring_to_string(pFilename);
        ocarina_string path = get_file_directory(file_name);//Paths::Normalize(UNICODE_TO_MULTIBYTE(pFilename));
        //file_name = get_file_name(file_name);
        //gVerify(Paths::ResolveRelativePaths(path), == true);

        auto existingInclude = std::find_if(IncludedFiles.begin(), IncludedFiles.end(), [&path](const ocarina_string &include) {
            return strcmp(include.c_str(), path.c_str()) == 0;
        });

        if (existingInclude != IncludedFiles.end()) {
            static const char nullStr[] = " ";
            pUtils->CreateBlobFromPinned(nullStr, ARRAYSIZE(nullStr), DXC_CP_ACP, pEncoding.GetAddressOf());
            *ppIncludeSource = pEncoding.Detach();
            return S_OK;
        }

        HRESULT hr = try_load_file(file_name.c_str(), &pEncoding);
        if (SUCCEEDED(hr)) {
            IncludedFiles.push_back(file_name);
            *ppIncludeSource = pEncoding.Detach();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

    ocarina_vector<ocarina_string> IncludedFiles;
    IDxcUtils* pUtils;
    ocarina_string shader_file_directory;
};

namespace {

// SPIR-V opcodes used by the shaderpdb DebugLine sanitize pass.
constexpr uint32_t kSpvOpExtInstImport = 11u;
constexpr uint32_t kSpvOpExtInst = 12u;
constexpr uint32_t kSpvOpTypeInt = 21u;
constexpr uint32_t kSpvOpConstant = 43u;
// NonSemantic.Shader.DebugInfo.100 DebugLine (see SPIR-V Headers).
constexpr uint32_t kNonSemanticDebugLine = 103u;

[[nodiscard]] bool spirv_literal_starts_with(
    const uint32_t *words,
    uint32_t word_count,
    const char *prefix) noexcept {
    if (words == nullptr || prefix == nullptr || word_count == 0) {
        return false;
    }
    const size_t prefix_len = std::strlen(prefix);
    const size_t byte_capacity = static_cast<size_t>(word_count) * sizeof(uint32_t);
    if (prefix_len > byte_capacity) {
        return false;
    }
    // SPIR-V string literals are little-endian packed chars (matches LE hosts).
    const char *bytes = reinterpret_cast<const char *>(words);
    for (size_t i = 0; i < prefix_len; ++i) {
        if (bytes[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_debug_set_id(
    const ocarina_vector<uint32_t> &debug_set_ids,
    uint32_t set_id) noexcept {
    for (uint32_t id : debug_set_ids) {
        if (id == set_id) {
            return true;
        }
    }
    return false;
}

constexpr uint32_t kSpvOpFunction = 54u;

/// DXC -fspv-debug=vulkan-with-source often emits DebugLine columns past the
/// DebugSource line length (Column Start and/or End). spirv-val / Vulkan
/// VUID-08737 then reject the module. spirv-val requires Column Start/End to
/// be 32-bit *unsigned* OpConstant ids, and treats every line's valid column
/// range as 1..length (length >= 1), so unsigned OpConstant(1) is always safe.
/// Line numbers are kept.
void sanitize_nonsemantic_debug_line_columns(ocarina_vector<uint32_t> &spirv) {
    if (spirv.size() < 5) {
        return;
    }

    ocarina_vector<uint32_t> debug_set_ids;
    uint32_t uint32_type_id = 0;
    uint32_t uint_one_id = 0;
    size_t first_function_offset = 0;

    for (size_t i = 5; i < spirv.size();) {
        const uint32_t word0 = spirv[i];
        const uint32_t word_count = word0 >> 16;
        const uint32_t opcode = word0 & 0xffffu;
        if (word_count == 0 || i + word_count > spirv.size()) {
            break;
        }

        if (opcode == kSpvOpExtInstImport && word_count >= 3) {
            if (spirv_literal_starts_with(
                    &spirv[i + 2],
                    word_count - 2,
                    "NonSemantic.Shader.DebugInfo.")) {
                debug_set_ids.push_back(spirv[i + 1]);
            }
        } else if (opcode == kSpvOpTypeInt && word_count >= 4) {
            // OpTypeInt <id> <width> <signedness> — DebugLine columns need unsigned.
            if (spirv[i + 2] == 32u && spirv[i + 3] == 0u && uint32_type_id == 0) {
                uint32_type_id = spirv[i + 1];
            }
        } else if (opcode == kSpvOpConstant && word_count >= 4 && uint_one_id == 0) {
            // OpConstant <type> <id> <value>
            if (uint32_type_id != 0 &&
                spirv[i + 1] == uint32_type_id &&
                spirv[i + 3] == 1u) {
                uint_one_id = spirv[i + 2];
            }
        } else if (opcode == kSpvOpFunction && first_function_offset == 0) {
            first_function_offset = i;
        }

        i += word_count;
    }

    if (debug_set_ids.empty()) {
        return;
    }

    // No existing %uint_1: allocate a new id and insert OpConstant before functions.
    if (uint_one_id == 0) {
        if (uint32_type_id == 0 || first_function_offset == 0) {
            spdlog::warn(
                "shaderpdb: no unsigned OpConstant(1) available; cannot sanitize DebugLine columns");
            return;
        }
        uint_one_id = spirv[3];
        spirv[3] = uint_one_id + 1u;
        const uint32_t op_constant_words[4] = {
            (4u << 16) | kSpvOpConstant,
            uint32_type_id,
            uint_one_id,
            1u,
        };
        spirv.insert(
            spirv.begin() + static_cast<std::ptrdiff_t>(first_function_offset),
            op_constant_words,
            op_constant_words + 4);
        spdlog::info("shaderpdb: injected unsigned OpConstant(1) id={}", uint_one_id);
    }

    size_t fixed = 0;
    for (size_t i = 5; i < spirv.size();) {
        const uint32_t word0 = spirv[i];
        const uint32_t word_count = word0 >> 16;
        const uint32_t opcode = word0 & 0xffffu;
        if (word_count == 0 || i + word_count > spirv.size()) {
            break;
        }
        // OpExtInst: type, id, set, instruction, Source, LineStart, LineEnd, ColStart, ColEnd
        if (opcode == kSpvOpExtInst && word_count >= 10) {
            const uint32_t set_id = spirv[i + 3];
            const uint32_t ext_inst = spirv[i + 4];
            if (ext_inst == kNonSemanticDebugLine && is_debug_set_id(debug_set_ids, set_id)) {
                if (spirv[i + 8] != uint_one_id || spirv[i + 9] != uint_one_id) {
                    spirv[i + 8] = uint_one_id;
                    spirv[i + 9] = uint_one_id;
                    ++fixed;
                }
            }
        }
        i += word_count;
    }

    if (fixed > 0) {
        spdlog::info(
            "shaderpdb: sanitized {} NonSemantic DebugLine column range(s) to unsigned column 1",
            fixed);
    }
}

} // namespace

bool DXCCompiler::compile_hlsl_spriv(const CompileInput &input, CompileResult &result) {
    ComPtr<IDxcUtils> dxc_utils = {};
    ComPtr<IDxcCompiler3> dxc_compiler = {};

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxc_compiler.ReleaseAndGetAddressOf()));

    ocarina_vector<LPCWSTR> args;
    args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-T");
    if (input.shader_type == ShaderType::VertexShader) {
        args.push_back(L"vs_6_0");
    } else if (input.shader_type == ShaderType::PixelShader) {
        args.push_back(L"ps_6_0");
    } else if (input.shader_type == ShaderType::ComputeShader) {
        args.push_back(L"cs_6_0");
    }

    // Keep wide strings alive until Compile returns (args stores LPCWSTR pointers).
    ocarina_vector<std::wstring> owned_wstrings;
    owned_wstrings.reserve(2 + input.include_paths.size() + input.macros.size());

    ocarina_string file_dir = get_file_directory(input.full_file_path);
    args.push_back(L"-I");
    owned_wstrings.emplace_back(string_to_wstring(file_dir).c_str());
    args.push_back(owned_wstrings.back().c_str());
    for (size_t i = 0; i < input.include_paths.size(); ++i) {
        args.push_back(L"-I");
        owned_wstrings.emplace_back(input.include_paths[i].begin(), input.include_paths[i].end());
        args.push_back(owned_wstrings.back().c_str());
    }

    args.push_back(L"-E");
    owned_wstrings.emplace_back(input.entry.begin(), input.entry.end());
    args.push_back(owned_wstrings.back().c_str());
    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.1");

    // RenderDoc Vulkan source debugging: NonSemantic.Shader.DebugInfo.100 is embedded
    // in the SPIR-V. DXC rejects -Fd with -spirv (no separate DXC_OUT_PDB).
    // Passing any -fspv-extension restricts the allowed set, so also permit
    // SPV_EXT_descriptor_indexing for bindless runtime arrays (g_textures[] / g_samplers[]).
    // -Vd: DXC's bundled spirv-val rejects some valid DebugLine column ranges from
    // vulkan-with-source (Column End > line length); skip validation so compile succeeds.
    // RenderDoc still consumes the embedded debug source in the .spv.
    if (input.output_pdbs) {
        args.push_back(DXC_ARG_DEBUG);
        args.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
        args.push_back(L"-Vd");
        args.push_back(L"-fspv-extension=SPV_KHR_non_semantic_info");
        args.push_back(L"-fspv-extension=SPV_EXT_descriptor_indexing");
        args.push_back(L"-fspv-debug=vulkan-with-source");
    }

    for (const auto& option : input.macros) {
        args.push_back(L"-D");
        owned_wstrings.emplace_back(option.begin(), option.end());
        args.push_back(owned_wstrings.back().c_str());
    }

    DxcBuffer src_buffer = {
        input.hlsl.data(),
        input.hlsl.size(),
        0};

    CustomIncludeHandler preprocessIncludeHandler(dxc_utils.Get());

    ComPtr<IDxcResult> operationResult;
    HRESULT hr = dxc_compiler->Compile(&src_buffer, args.data(), args.size(), &preprocessIncludeHandler, IID_PPV_ARGS(&operationResult));

    ComPtr<IDxcBlobUtf8> errors = nullptr;
    hr = operationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    if (errors != nullptr && errors->GetStringLength() > 0) {
        result.error = errors->GetStringPointer();
        spdlog::error(
            "DXC compile failed: file='{}' entry='{}' stage={} error=\"{}\"",
            input.full_file_path,
            input.entry,
            static_cast<int>(input.shader_type),
            result.error);
    } else if (FAILED(hr)) {
        spdlog::error(
            "DXC compile failed: file='{}' entry='{}' stage={} (no DXC_OUT_ERRORS string available, hr=0x{:x})",
            input.full_file_path,
            input.entry,
            static_cast<int>(input.shader_type),
            static_cast<unsigned>(hr));
    }

    if (FAILED(hr)) {
        // Avoid pretending compilation succeeded when DXC returned failure.
        return false;
    }

    ComPtr<IDxcBlob> shader_obj;
    operationResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_obj), nullptr);
    if (shader_obj == nullptr || shader_obj->GetBufferSize() == 0) {
        // DXC success but no usable object: treat as failure and keep result.error if set.
        if (result.error.empty()) {
            result.error = "DXC returned no object blob.";
        }
        spdlog::error(
            "DXC compile produced empty object: file='{}' entry='{}' stage={} error=\"{}\"",
            input.full_file_path,
            input.entry,
            static_cast<int>(input.shader_type),
            result.error);
        return false;
    }
    result.spriv_codes.resize(shader_obj->GetBufferSize() / sizeof(uint32_t));

    for (size_t i = 0; i < result.spriv_codes.size(); ++i) {
        uint32_t spvCode = static_cast<uint32_t *>(shader_obj->GetBufferPointer())[i];
        result.spriv_codes[i] = spvCode;
    }

    if (input.output_pdbs) {
        // Make debug SPIR-V pass vkCreateShaderModule / spirv-val (VUID-08737).
        sanitize_nonsemantic_debug_line_columns(result.spriv_codes);
        spdlog::info(
            "shaderpdb: RenderDoc debug source embedded in SPIR-V (file='{}' entry='{}')",
            input.full_file_path,
            input.entry);
    }

    return result.spriv_codes.size() > 0;
}

void DXCCompiler::run_spriv_reflection(const ocarina_vector<uint32_t> &spriv, ShaderType shader_type, ShaderReflection &shader_reflection) {
    const void *shader_data = static_cast<const void*>(spriv.data());
    uint32_t shader_data_size = spriv.size() * sizeof(uint32_t);

    class SpirvReflector : public spirv_cross::Compiler {
    public:
        using Compiler::Compiler;

        /// True when every member has DecorationOffset (required by get_declared_struct_size).
        /// Debug SPIR-V (-fspv-debug=vulkan-with-source) adds named structs without offsets;
        /// calling get_declared_struct_size on those throws.
        [[nodiscard]] bool struct_has_member_offsets(const spirv_cross::SPIRType &type) const {
            if (type.basetype != spirv_cross::SPIRType::Struct || type.member_types.empty()) {
                return false;
            }
            for (uint32_t member_index = 0; member_index < type.member_types.size(); ++member_index) {
                if (!has_member_decoration(type.self, member_index, spv::DecorationOffset)) {
                    return false;
                }
            }
            return true;
        }

        void collect_named_structs(
            ShaderReflection &shader_reflection,
            ShaderVariableType (*to_variable_type)(uint32_t, uint32_t, const spirv_cross::SPIRType &)) {
            ir.for_each_typed_id<spirv_cross::SPIRType>(
                [&](uint32_t type_id, const spirv_cross::SPIRType &type) {
                    if (type.basetype != spirv_cross::SPIRType::Struct || type.member_types.empty()) {
                        return;
                    }
                    if (get_decoration_bitset(type_id).get(spv::DecorationBlock) ||
                        get_decoration_bitset(type_id).get(spv::DecorationBufferBlock) ||
                        get_decoration_bitset(type.self).get(spv::DecorationBlock) ||
                        get_decoration_bitset(type.self).get(spv::DecorationBufferBlock)) {
                        return;
                    }
                    // Skip debug / non-layout structs that lack Offset decorations.
                    if (!struct_has_member_offsets(type)) {
                        return;
                    }

                    ocarina_string name = ocarina::to_ocarina_string(get_name(type_id));
                    if (name.empty()) {
                        name = ocarina::to_ocarina_string(get_name(type.self));
                    }
                    if (name.empty()) {
                        return;
                    }

                    for (const ShaderReflection::UniformBuffer &existing : shader_reflection.named_structs) {
                        if (existing.name == name) {
                            return;
                        }
                    }

                    ShaderReflection::UniformBuffer named_struct{};
                    named_struct.name = std::move(name);
                    named_struct.size = static_cast<uint32_t>(get_declared_struct_size(type));

                    for (uint32_t member_index = 0; member_index < type.member_types.size(); ++member_index) {
                        const spirv_cross::SPIRType &member_type = get_type(type.member_types[member_index]);
                        if (member_type.basetype == spirv_cross::SPIRType::Struct) {
                            continue;
                        }
                        ShaderReflection::ShaderVariable shader_variable{};
                        shader_variable.name = get_member_name(type.self, member_index);
                        if (shader_variable.name.empty()) {
                            shader_variable.name = get_member_name(type_id, member_index);
                        }
                        shader_variable.size = get_declared_struct_member_size(type, member_index);
                        shader_variable.offset = type_struct_member_offset(type, member_index);
                        shader_variable.variable_type =
                            to_variable_type(member_type.vecsize, member_type.columns, member_type);
                        named_struct.shader_variables.emplace_back(std::move(shader_variable));
                    }

                    if (!named_struct.shader_variables.empty()) {
                        shader_reflection.named_structs.emplace_back(std::move(named_struct));
                    }
                });
        }
    };

    SpirvReflector spirvmodule((const uint32_t *)shader_data, shader_data_size / 4);

    // The SPIR-V is now parsed, and we can perform reflection on it.
    ocarina_string entryPointName;
    spv::ExecutionModel executionModel;
    switch (shader_type) {
        case ShaderType::VertexShader:
            executionModel = spv::ExecutionModelVertex;
            break;
        case ShaderType::PixelShader:
            executionModel = spv::ExecutionModelFragment;
            break;
        case ShaderType::ComputeShader:
            executionModel = spv::ExecutionModelGLCompute;
            break;
        default:
            break;
    }

    //spirv_cross::SPIREntryPoint &execution = spirvmodule.get_entry_point(entryPointName, executionModel);
    spirv_cross::SmallVector<spirv_cross::EntryPoint> entrypoints = spirvmodule.get_entry_points_and_stages();
    spirv_cross::EntryPoint &entryPoint = entrypoints[0];

    spirv_cross::SPIREntryPoint &execution = spirvmodule.get_entry_point(entryPoint.name, entryPoint.execution_model);

    // remove unused variables
    auto active = spirvmodule.get_active_interface_variables();
    spirv_cross::ShaderResources resources = spirvmodule.get_shader_resources(active);
    spirvmodule.set_enabled_interface_variables(move(active));

    if (shader_type == ShaderType::ComputeShader) {
        spirv_cross::SpecializationConstant wg_x, wg_y, wg_z;
        spirvmodule.get_work_group_size_specialization_constants(wg_x, wg_y, wg_z);

        //reflectionData.m_ThreadGroupSize[0] = execution.workgroup_size.x;
        //reflectionData.m_ThreadGroupSize[1] = execution.workgroup_size.y;
        //reflectionData.m_ThreadGroupSize[2] = execution.workgroup_size.z;
        shader_reflection.thread_group_size[0] = execution.workgroup_size.x;
        shader_reflection.thread_group_size[1] = execution.workgroup_size.y;
        shader_reflection.thread_group_size[2] = execution.workgroup_size.z;
    }

    for (spirv_cross::Resource &resource : resources.uniform_buffers) {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        const spirv_cross::SPIRType &type = spirvmodule.get_type(resource.base_type_id);
        // HLSL `ByteAddressBuffer` reflection may appear here as a non-struct buffer.
        // `get_declared_struct_size()` expects a struct type with Offset decorations.
        uint32_t size = 0;
        if (type.basetype == spirv_cross::SPIRType::Struct
            && spirvmodule.struct_has_member_offsets(type)) {
            size = static_cast<uint32_t>(spirvmodule.get_declared_struct_size(type));
        }

        ShaderReflection::UniformBuffer ubo{};
        ubo.name = spirvmodule.get_name(resource.id);
        ubo.size = size;
        ubo.binding = binding;
        ubo.descriptor_set = static_cast<uint8_t>(set);

        auto append_leaf_uniform_members =
            [&](auto&& self, spirv_cross::TypeID struct_type_id, uint32_t base_offset) -> void {
            const spirv_cross::SPIRType& struct_type = spirvmodule.get_type(struct_type_id);
            for (uint32_t member_index = 0; member_index < struct_type.member_types.size(); ++member_index) {
                const spirv_cross::SPIRType& leaf_type =
                    spirvmodule.get_type(struct_type.member_types[member_index]);
                const uint32_t member_offset =
                    base_offset + spirvmodule.type_struct_member_offset(struct_type, member_index);

                if (leaf_type.basetype == spirv_cross::SPIRType::Struct) {
                    self(self, struct_type.member_types[member_index], member_offset);
                    continue;
                }

                ShaderReflection::ShaderVariable shader_variable{};
                shader_variable.name = spirvmodule.get_member_name(struct_type_id, member_index);
                shader_variable.size = spirvmodule.get_declared_struct_member_size(struct_type, member_index);
                shader_variable.offset = member_offset;
                shader_variable.variable_type =
                    get_shader_variable_type(leaf_type.vecsize, leaf_type.columns, leaf_type);
                ubo.shader_variables.emplace_back(std::move(shader_variable));
            }
        };

        if (size > 0) {
            for (uint32_t member_index = 0; member_index < type.member_types.size(); ++member_index) {
                const spirv_cross::SPIRType& member_type =
                    spirvmodule.get_type(type.member_types[member_index]);
                const uint32_t member_offset = spirvmodule.type_struct_member_offset(type, member_index);

                if (member_type.basetype == spirv_cross::SPIRType::Struct) {
                    append_leaf_uniform_members(
                        append_leaf_uniform_members, type.member_types[member_index], member_offset);
                    continue;
                }

                ShaderReflection::ShaderVariable shader_variable{};
                shader_variable.name = spirvmodule.get_member_name(resource.base_type_id, member_index);
                shader_variable.size = spirvmodule.get_declared_struct_member_size(type, member_index);
                shader_variable.offset = member_offset;
                shader_variable.variable_type =
                    get_shader_variable_type(member_type.vecsize, member_type.columns, member_type);
                ubo.shader_variables.emplace_back(std::move(shader_variable));
            }
        }
        shader_reflection.uniform_buffers.emplace_back(std::move(ubo));
    }

    auto add_named_struct = [&](
        uint32_t type_id,
        const spirv_cross::SPIRType &struct_type,
        const char *fallback_name = nullptr) {
        if (struct_type.basetype != spirv_cross::SPIRType::Struct || struct_type.member_types.empty()) {
            return;
        }
        if (!spirvmodule.struct_has_member_offsets(struct_type)) {
            return;
        }

        ocarina_string name = ocarina::to_ocarina_string(spirvmodule.get_name(type_id));
        if (name.empty()) {
            name = ocarina::to_ocarina_string(spirvmodule.get_name(struct_type.self));
        }
        if (name.empty() && fallback_name != nullptr) {
            name = fallback_name;
        }
        if (name.empty()) {
            return;
        }
        for (const ShaderReflection::UniformBuffer &existing : shader_reflection.named_structs) {
            if (existing.name == name) {
                return;
            }
        }

        ShaderReflection::UniformBuffer named_struct{};
        named_struct.name = std::move(name);
        named_struct.size = static_cast<uint32_t>(spirvmodule.get_declared_struct_size(struct_type));
        for (uint32_t member_index = 0; member_index < struct_type.member_types.size(); ++member_index) {
            const spirv_cross::SPIRType &member_type = spirvmodule.get_type(struct_type.member_types[member_index]);
            if (member_type.basetype == spirv_cross::SPIRType::Struct) {
                continue;
            }
            ShaderReflection::ShaderVariable shader_variable{};
            shader_variable.name = spirvmodule.get_member_name(struct_type.self, member_index);
            if (shader_variable.name.empty()) {
                shader_variable.name = spirvmodule.get_member_name(type_id, member_index);
            }
            shader_variable.size = spirvmodule.get_declared_struct_member_size(struct_type, member_index);
            shader_variable.offset = spirvmodule.type_struct_member_offset(struct_type, member_index);
            shader_variable.variable_type =
                get_shader_variable_type(member_type.vecsize, member_type.columns, member_type);
            named_struct.shader_variables.emplace_back(std::move(shader_variable));
        }
        if (!named_struct.shader_variables.empty()) {
            shader_reflection.named_structs.emplace_back(std::move(named_struct));
        }
    };

    for (spirv_cross::Resource &resource : resources.storage_buffers) {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::StorageBuffer;
        shader_resource.shader_type = (uint32_t)shader_type;

        const spirv_cross::SPIRType &type = spirvmodule.get_type(resource.base_type_id);
        if (!type.member_types.empty()) {
            const spirv_cross::SPIRType &member_type =
                spirvmodule.get_type(type.member_types[0]);

            // HLSL ByteAddressBuffer is a raw byte SRV/UAV; it is not a struct,
            // and spirv_cross cannot compute struct sizes for it.
            if (member_type.basetype != spirv_cross::SPIRType::Struct) {
                shader_resource.size = 0;
            } else {
                // StructuredBuffer<Element> → array of Element, or array of struct wrapper.
                uint32_t element_type_id = type.member_types[0];
                const spirv_cross::SPIRType *element_type = &member_type;
                if (!member_type.member_types.empty() &&
                    spirvmodule.get_type(member_type.member_types[0]).basetype ==
                        spirv_cross::SPIRType::Struct) {
                    element_type_id = member_type.member_types[0];
                    element_type = &spirvmodule.get_type(element_type_id);
                }
                if (element_type->basetype == spirv_cross::SPIRType::Struct
                    && spirvmodule.struct_has_member_offsets(*element_type)) {
                    shader_resource.size = static_cast<uint32_t>(
                        spirvmodule.get_declared_struct_size(*element_type));
                    add_named_struct(element_type_id, *element_type, nullptr);
                } else {
                    shader_resource.size = 0;
                }
            }
        } else {
            shader_resource.size = 0;
        }

        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    spirvmodule.collect_named_structs(shader_reflection, &DXCCompiler::get_shader_variable_type);

    for (spirv_cross::Resource &resource : resources.push_constant_buffers) {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        const spirv_cross::SPIRType &type = spirvmodule.get_type(resource.base_type_id);
        if (type.basetype != spirv_cross::SPIRType::Struct
            || !spirvmodule.struct_has_member_offsets(type)) {
            continue;
        }
        uint32_t size = static_cast<uint32_t>(spirvmodule.get_declared_struct_size(type));

        ShaderReflection::UniformBuffer push_constant{};
        push_constant.name = spirvmodule.get_name(resource.id);
        push_constant.size = size;
        push_constant.offset = spirvmodule.get_decoration(resource.id, spv::DecorationOffset);

        spv::ExecutionModel stage = spirvmodule.get_execution_model();

        // Now loop through all members
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
            ShaderReflection::ShaderVariable shader_variable{};
            shader_variable.name = spirvmodule.get_member_name(resource.base_type_id, i);
            shader_variable.size = spirvmodule.get_declared_struct_member_size(type, i);

            shader_variable.offset = spirvmodule.type_struct_member_offset(type, i);

            const spirv_cross::SPIRType &member_type = spirvmodule.get_type(type.member_types[i]);
            uint32_t vec_size = member_type.vecsize;// e.g. 4 for float4
            uint32_t columns = member_type.columns; // 4 columns = matrix
            uint32_t array_size = member_type.array.size() > 0 ? member_type.array[0] : 0;

            shader_variable.variable_type = get_shader_variable_type(vec_size, columns, member_type);
            push_constant.shader_variables.emplace_back(std::move(shader_variable));
        }
        shader_reflection.push_constant_buffers.emplace_back(std::move(push_constant));
    }

    for (spirv_cross::Resource& resource : resources.storage_images)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::UAV;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    for (spirv_cross::Resource& resource : resources.sampled_images)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::SRV;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    for (spirv_cross::Resource& resource : resources.separate_images)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);
        auto &type = spirvmodule.get_type(resource.type_id);
        
        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::SRV;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_resource.is_separate_image = true;
        if (!type.array.empty()) {
            //is a texture array
            if (type.array[0] == 0)
            {
                shader_resource.is_bindless = true;
            } else {
                shader_resource.array_size = type.array[0];
            }
        }
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    for (spirv_cross::Resource& resource : resources.separate_samplers)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);
        auto& type = spirvmodule.get_type(resource.type_id);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::Sampler;
        shader_resource.shader_type = (uint32_t)shader_type;
        if (!type.array.empty() && type.array[0] == 0) {
            shader_resource.is_bindless = true;
        }
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    if (shader_type == ShaderType::VertexShader)
    {
        uint32_t offset = 0;
        uint32_t vertex_attrib_index = 0;
        uint32_t total_size = 0;
        for (spirv_cross::Resource& resource : resources.stage_inputs)
        {
            uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);
            uint32_t location = spirvmodule.get_decoration(resource.id, spv::DecorationLocation);
            uint32_t offset_in_hlsl = spirvmodule.get_decoration(resource.id, spv::DecorationOffset);
            spirv_cross::SPIRType spirType = spirvmodule.get_type(resource.type_id);

            VertexFormat format = VertexFormat::Undefined;

            if (spirType.basetype == spirv_cross::SPIRType::Float) {
                if (spirType.vecsize == 1)
                    format = VertexFormat::R32_SFLOAT;
                else if (spirType.vecsize == 2)
                    format = VertexFormat::R32G32_SFLOAT;
                else if (spirType.vecsize == 3)
                    format = VertexFormat::R32G32B32_SFLOAT;
                else if (spirType.vecsize == 4)
                    format = VertexFormat::R32G32B32A32_SFLOAT;
            } else if (spirType.basetype == spirv_cross::SPIRType::Int) {
                if (spirType.vecsize == 1)
                    format = VertexFormat::R32_SINT;
                else if (spirType.vecsize == 2)
                    format = VertexFormat::R32G32_SINT;
                else if (spirType.vecsize == 3)
                    format = VertexFormat::R32G32B32_SINT;
                else if (spirType.vecsize == 4)
                    format = VertexFormat::R32G32B32A32_SINT;
            } else if (spirType.basetype == spirv_cross::SPIRType::UInt) {
                if (spirType.vecsize == 1)
                    format = VertexFormat::R32_UINT;
                else if (spirType.vecsize == 2)
                    format = VertexFormat::R32G32_UINT;
                else if (spirType.vecsize == 3)
                    format = VertexFormat::R32G32B32_UINT;
                else if (spirType.vecsize == 4)
                    format = VertexFormat::R32G32B32A32_UINT;
            }

            uint32_t size = spirType.vecsize * 4;

            if (vertex_attrib_index > 0 && offset_in_hlsl == 0) {
                offset += total_size;
            } else {
                offset = offset_in_hlsl;
            }

            ShaderReflection::ShaderResource shader_resource;
            shader_resource.name = spirvmodule.get_name(resource.id);
            shader_resource.descriptor_set = set;
            shader_resource.register_ = binding;
            shader_resource.location = location;
            shader_resource.offset = offset;
            shader_resource.shader_type = (uint32_t)shader_type;
            shader_resource.parameter_type = ShaderReflection::ResourceType::InputAttachment;
            shader_resource.format = format;
            ocarina_string semantic;
            if (spirvmodule.has_decoration(resource.id, spv::DecorationHlslSemanticGOOGLE)) {
                semantic = spirvmodule.get_decoration_string(resource.id, spv::DecorationHlslSemanticGOOGLE);
            } else {
                if (shader_resource.name.find("in.var.") == 0) {
                    semantic = shader_resource.name.substr(strlen("in.var."));
                } else {
                    semantic = shader_resource.name;
                }
            }
            shader_resource.vertex_attribute_type = VertexAttributeType::from_string(semantic.c_str());

            shader_reflection.input_layouts.push_back(std::move(shader_resource));

            vertex_attrib_index++;
            total_size += size;
        }
    }
}

ShaderVariableType DXCCompiler::get_shader_variable_type(uint32_t vec_size, uint32_t column_size, const spirv_cross::SPIRType &type) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        if (vec_size == 1 && column_size == 1)
            return ShaderVariableType::FLOAT;
        else if (vec_size == 2 && column_size == 1)
            return ShaderVariableType::FLOAT2;
        else if (vec_size == 3 && column_size == 1)
            return ShaderVariableType::FLOAT3;
        else if (vec_size == 4 && column_size == 1)
            return ShaderVariableType::FLOAT4;
        else if (vec_size == 4 && column_size == 4)
            return ShaderVariableType::FLOAT4X4;
    } else if (type.basetype == spirv_cross::SPIRType::Int || type.basetype == spirv_cross::SPIRType::UInt) {
        if (vec_size == 1 && column_size == 1)
            return ShaderVariableType::INT;
        else if (vec_size == 2 && column_size == 1)
            return ShaderVariableType::INT2;
        else if (vec_size == 3 && column_size == 1)
            return ShaderVariableType::INT3;
        else if (vec_size == 4 && column_size == 1)
            return ShaderVariableType::INT4;
    }
    return ShaderVariableType::FLOAT;
}

}// namespace ocarina