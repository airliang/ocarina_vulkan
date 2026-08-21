//
// Created by Zero on 06/08/2022.
//

#include "vulkan_shader.h"
#include "util.h"
#include "vulkan_device.h"
#include "shader_compiler.h"
#include <algorithm>
#include <numeric>
#include <cstring>

namespace ocarina {

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
    CompiledShader compiled{};
    if (!compile_hlsl_to_spirv_and_reflect(filename, shader_type, entry_point, compiled)) {
        return nullptr;
    }

    std::span<uint32_t> shader_code(compiled.spirv);
    VulkanShader* vulkan_shader = VulkanShader::create(device, shader_type, shader_code, entry_point);
    if (vulkan_shader == nullptr) {
        return nullptr;
    }

    vulkan_shader->get_shader_variables(compiled.reflection);
    if (shader_type == ShaderType::VertexShader) {
        vulkan_shader->get_vertex_attributes(compiled.reflection);
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
            // A separate Texture2D pairs with its own SamplerState binding, so the layout must
            // declare SAMPLED_IMAGE; COMBINED_IMAGE_SAMPLER makes descriptor writes mismatch.
            variable.type = (shader_resource.is_bindless || shader_resource.is_separate_image)
                ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::UAV) {
            variable.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::Sampler) {
            variable.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::StorageBuffer) {
            variable.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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

    named_structs_ = reflection.named_structs;

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

bool VulkanShader::get_struct_members(
    const char* struct_name,
    std::vector<RHIShader::UniformBufferMember>& members,
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
            RHIShader::UniformBufferMember member;
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

}// namespace ocarina