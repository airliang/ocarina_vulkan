//
// Created by Zero on 06/08/2022.
//

#include "vulkan_shader.h"
#include "util.h"
#include "vulkan_device.h"
#include <algorithm>
#include <numeric>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include "dxcapi.h"

#include <wrl/client.h>
using namespace Microsoft::WRL;
#endif

#include "core/logging.h"
#include "dxc_compiler.h"
#include <cstring>
#include <fstream>

namespace ocarina {

namespace {

std::string get_spv_path_for_shader(const std::string& shader_file_path) {
    return shader_file_path + ".spv";
}

bool load_spirv_from_file(const std::string& spv_path, std::vector<uint32_t>& spirv_code) {
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
    input.read(reinterpret_cast<char*>(spirv_code.data()), file_size);
    return input.good();
}

bool save_spirv_to_file(const std::string& spv_path, const std::vector<uint32_t>& spirv_code) {
    if (spirv_code.empty()) {
        return false;
    }

    std::ofstream output(spv_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output.write(
        reinterpret_cast<const char*>(spirv_code.data()),
        static_cast<std::streamsize>(spirv_code.size() * sizeof(uint32_t)));
    return output.good();
}

bool compile_hlsl_file_to_spirv(
    const std::string& filename,
    ShaderType shader_type,
    const std::string& entry_point,
    std::vector<uint32_t>& spirv_code)
{
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
        return false;
    }

    spirv_code = std::move(compile_result.spriv_codes);
    return !spirv_code.empty();
}

}// namespace

VulkanShader::VulkanShader(VulkanDevice *device, std::span<uint32_t> shaderCode, const std::string_view &entryPoint, VkShaderStageFlagBits stage) : 
    entry_(entryPoint), device_(device), stage_(stage) {
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = shaderCode.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = (uint32_t *)shaderCode.data();
    vkCreateShaderModule(device->logicalDevice(), &moduleCreateInfo, nullptr, &shader_module_);
}

VulkanShader::~VulkanShader() {
    vkDestroyShaderModule(device_->logicalDevice(), shader_module_, nullptr);
}

VulkanShader* VulkanShader::create(Device::Impl* device,
                                   ShaderType shader_type, 
    std::span<uint32_t> shaderCode,
    const std::string_view& entryPoint)
{
    return ocarina::new_with_allocator<VulkanShader>(static_cast<VulkanDevice *>(device), shaderCode, entryPoint, get_vulkan_shader_stage(shader_type));
}

VulkanShader *VulkanShader::create_from_HLSL(Device::Impl *device, ShaderType shader_type, const std::string &filename, const std::string &entry_point) {
    const std::string spv_path = get_spv_path_for_shader(filename);
    std::vector<uint32_t> spirv_code;

    if (!load_spirv_from_file(spv_path, spirv_code)) {
        if (!compile_hlsl_file_to_spirv(filename, shader_type, entry_point, spirv_code)) {
            return nullptr;
        }
        if (!save_spirv_to_file(spv_path, spirv_code)) {
            OC_ERROR_FORMAT("Failed to write SPIR-V cache file: {}", spv_path.c_str());
        }
    }

    ShaderReflection reflection;
    DXCCompiler::run_spriv_reflection(spirv_code, shader_type, reflection);

    VulkanShader* vulkan_shader = VulkanShader::create(device, shader_type, spirv_code, entry_point);
    if (vulkan_shader == nullptr) {
        return nullptr;
    }

    vulkan_shader->get_shader_variables(reflection);
    if (shader_type == ShaderType::VertexShader) {
        vulkan_shader->get_vertex_attributes(reflection);
        vulkan_shader->create_vertex_stream_binding();
    }
    return vulkan_shader;
}

void VulkanShader::get_shader_variables(const ShaderReflection &reflection) {

    VulkanShaderVariableBinding variable;
    variable.shader_stage = stage_;
    for (auto& shader_resource : reflection.shader_resources)
    {
        strcpy(variable.name, shader_resource.name.c_str());
        variable.binding = shader_resource.binding;
        variable.descriptor_set = shader_resource.descriptor_set;
        variable.size = shader_resource.size;
        variable.count = 1;
        variable.is_bindless = shader_resource.is_bindless;
        
        if (shader_resource.parameter_type == ShaderReflection::ResourceType::ConstantBuffer)
        {
            variable.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::SRV)
        {
            variable.type = shader_resource.is_bindless
                ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::UAV) {
            variable.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::Sampler) {
            variable.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        if (variable.is_bindless) {
            variable.count = get_vulkan_bindless_resource_max_count(variable.type);
        }

        variables_.push_back(variable);
    }

    for (auto& ubo : reflection.uniform_buffers)
    {
        VulkanShaderVariableBinding variable;
        strcpy(variable.name, ubo.name.c_str());
        variable.binding = ubo.binding;
        variable.descriptor_set = ubo.descriptor_set;
        variable.size = ubo.size;
        variable.count = 1;// UBO is always 1
        variable.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        variable.shader_stage = stage_;
        variable.shader_variables_ = std::move(ubo.shader_variables);
        variable.is_bindless = ubo.is_bindless;
        variables_.push_back(variable);
    }

    for (auto& push_constant : reflection.push_constant_buffers)
    {
        PushConstant pc;
        pc.offset = push_constant.offset;
        pc.size = push_constant.size;
        pc.shader_variables = std::move(push_constant.shader_variables);
        pc.name = push_constant.name;
        pc.stage_flags = stage_;
        push_constants_.push_back(pc);
    }
}

void VulkanShader::get_vertex_attributes(const ShaderReflection& reflection)
{
    VertexAttribute attrib;
    for (auto shader_resource : reflection.input_layouts) {
        if (shader_resource.parameter_type == ShaderReflection::ResourceType::InputAttachment) {
            attrib.binding = shader_resource.register_;
            attrib.location = shader_resource.location;
            attrib.offset = shader_resource.offset;
            attrib.format = shader_resource.format;
            attrib.type = (uint8_t)shader_resource.vertex_attribute_type;

            vertex_attributes_.push_back(attrib);
        } 
    }
}

bool VulkanShader::HLSLToSPRIV(std::span<char> hlsl, VkShaderStageFlagBits stage, const std::string_view &entryPoint, bool outputSymbols, 
    std::vector<uint32_t> &outSpriv, std::string &errorLog) {
    ComPtr<IDxcUtils> dxc_utils = {};
    ComPtr<IDxcCompiler3> dxc_compiler = {};
    
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxc_compiler.ReleaseAndGetAddressOf()));

    std::vector<LPCWSTR> args;
    args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-T");
    if (stage == VK_SHADER_STAGE_VERTEX_BIT)
    {
        args.push_back(L"vs_6_0");
    }
    else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
    {
        args.push_back(L"ps_6_0");
    }
    else if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
    {
        args.push_back(L"cs_6_0");
    }

    std::wstring wEntry(entryPoint.begin(), entryPoint.end());
    args.push_back(L"-E");
    args.push_back(wEntry.c_str());

    if (outputSymbols)
    {
        args.push_back(L"-Zi");
    }

    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.1");

    DxcBuffer src_buffer = {
        hlsl.data(),
        hlsl.size(),
        0};

    IDxcIncludeHandler* dxcIncludeHandler = nullptr;

    ComPtr<IDxcResult> operationResult;
    HRESULT hr = dxc_compiler->Compile(&src_buffer, args.data(), args.size(), dxcIncludeHandler, IID_PPV_ARGS(&operationResult));

    ComPtr<IDxcBlob> shader_obj;
    operationResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_obj), nullptr);

    ComPtr<IDxcBlobUtf8> errors = nullptr;
    operationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    if (errors != nullptr && errors->GetStringLength() > 0)
    {
        errorLog = errors->GetStringPointer();
        return false;
    }

    outSpriv.resize(shader_obj->GetBufferSize() / sizeof(uint32_t));

    for (size_t i = 0; i < outSpriv.size(); ++i)
    {
        uint32_t spvCode = static_cast<uint32_t *>(shader_obj->GetBufferPointer())[i];
        outSpriv[i] = spvCode;
    }

    return true;
}

void VulkanShader::create_vertex_stream_binding() {
    size_t attr_count = get_vertex_attribute_count();

    vertex_stream_binding_.attribute_descriptions_.resize(attr_count);
    vertex_stream_binding_.binding_descriptions_.resize(attr_count);
    //vertex_stream_binding_.buffers_.resize(attr_count);
    vertex_stream_binding_.offsets_.resize(attr_count);
    vertex_stream_binding_.attribute_types_.resize(attr_count);

    std::vector<size_t> attribute_order(attr_count);
    std::iota(attribute_order.begin(), attribute_order.end(), size_t{0});
    std::sort(attribute_order.begin(), attribute_order.end(), [this](size_t lhs, size_t rhs) {
        return get_vertex_attribute(lhs).location < get_vertex_attribute(rhs).location;
    });

    for (size_t i = 0; i < attr_count; ++i)
    {
        auto attr = get_vertex_attribute(attribute_order[i]);
        vertex_stream_binding_.attribute_descriptions_[i].binding = static_cast<uint32_t>(i);
        vertex_stream_binding_.attribute_descriptions_[i].location = attr.location;
        vertex_stream_binding_.attribute_descriptions_[i].format = static_cast<VkFormat>(attr.format);
        vertex_stream_binding_.attribute_descriptions_[i].offset = 0;
        vertex_stream_binding_.attribute_types_[i] = (VertexAttributeType::Enum)attr.type;
        vertex_stream_binding_.offsets_[i] = 0;

        vertex_stream_binding_.binding_descriptions_[i].binding = i;
        vertex_stream_binding_.binding_descriptions_[i].stride = get_vulkan_format_size(static_cast<VkFormat>(attr.format));
        vertex_stream_binding_.binding_descriptions_[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }
}

VulkanShader* VulkanShaderManager::get_or_create_from_HLSL(VulkanDevice *device,
    ShaderType shader_type,
    const std::string& filename,
    const std::set<std::string> &options,
    const std::string& entry_point)
{
    ShaderKey shader_key{shader_type, filename, entry_point, options};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = vulkan_shaders_.find(shader_key);
        if (it != vulkan_shaders_.end()) {
            return it->second;
        }
    }

    VulkanShader* shader = VulkanShader::create_from_HLSL(device, shader_type, filename, entry_point);
    if (shader == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vulkan_shaders_.find(shader_key);
    if (it != vulkan_shaders_.end()) {
        ocarina::delete_with_allocator(shader);
        return it->second;
    }

    shaders_.insert({reinterpret_cast<handle_ty>(shader->shader_module()), shader});
    vulkan_shaders_.insert(std::make_pair(shader_key, shader));
    vulkan_shader_entries_.insert(std::make_pair(
        reinterpret_cast<handle_ty>(shader->shader_module()),
        VulkanShaderEntry{shader->shader_module(), shader->stage(), shader->get_entry_point()}));
    return shader;
}

VulkanShaderEntry VulkanShaderManager::get_shader_entry(handle_ty shader_handle) const
{
    auto it = vulkan_shader_entries_.find(shader_handle);
    if (it != vulkan_shader_entries_.end())
    {
        return it->second;
    }

    return {};
}

void VulkanShaderManager::clear(VulkanDevice* device)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iter : vulkan_shaders_)
    {
        ocarina::delete_with_allocator(iter.second);
    }
    vulkan_shaders_.clear();
    vulkan_shader_entries_.clear();
    shaders_.clear();
}

bool VulkanShader::get_uniform_buffer_members(
    const char* buffer_name,
    std::vector<RHIShader::UniformBufferMember>& members,
    uint32_t& buffer_size) const {
    members.clear();
    buffer_size = 0;
    if (buffer_name == nullptr) {
        return false;
    }

    for (const VulkanShaderVariableBinding& binding : variables_) {
        if (binding.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            continue;
        }
        if (std::strcmp(binding.name, buffer_name) != 0) {
            continue;
        }

        buffer_size = binding.size;
        members.reserve(binding.shader_variables_.size());
        for (const ShaderReflection::ShaderVariable& variable : binding.shader_variables_) {
            RHIShader::UniformBufferMember member;
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

}// namespace ocarina