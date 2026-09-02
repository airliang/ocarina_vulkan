//
// Created by Zero on 06/08/2022.
//

#include "vulkan_shader.h"
#include "util.h"
#include "vulkan_device.h"
#include "vulkan_driver.h"
#include "core/hash.h"
#include <algorithm>
#include <cstring>
#include <numeric>

namespace ocarina {

namespace {

const std::vector<uint32_t>* spirv_for_stage(const ShaderProgram* program, ShaderType shader_type) {
    if (program == nullptr) {
        return nullptr;
    }
    switch (shader_type) {
        case ShaderType::VertexShader:
            return &program->vertex_spirv();
        case ShaderType::PixelShader:
            return &program->pixel_spirv();
        case ShaderType::ComputeShader:
            return &program->compute_spirv();
        default:
            return nullptr;
    }
}

} // namespace

VulkanShader::VulkanShader(
    VulkanDevice* device,
    ShaderProgram* program,
    std::span<uint32_t> shaderCode,
    const std::string_view& entryPoint,
    VkShaderStageFlagBits stage,
    ShaderType shader_type)
    : RHIShader(program, shader_type),
      entry_(entryPoint),
      device_(device),
      stage_(stage) {
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = shaderCode.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = shaderCode.data();
    vkCreateShaderModule(device->logicalDevice(), &moduleCreateInfo, nullptr, &shader_module_);

    build_stage_bindings_from_program();
    if (shader_type == ShaderType::VertexShader) {
        create_vertex_stream_binding();
    }
}

VulkanShader::~VulkanShader() {
    vkDestroyShaderModule(device_->logicalDevice(), shader_module_, nullptr);
}

VulkanShader* VulkanShader::create_for_program(
    VulkanDevice* device,
    ShaderProgram* program,
    ShaderType shader_type) {
    if (device == nullptr || program == nullptr) {
        return nullptr;
    }

    const std::vector<uint32_t>* spirv = spirv_for_stage(program, shader_type);
    if (spirv == nullptr || spirv->empty()) {
        return nullptr;
    }

    std::span<uint32_t> shader_code(const_cast<uint32_t*>(spirv->data()), spirv->size());
    VulkanShader* vulkan_shader = ocarina::new_with_allocator<VulkanShader>(
        device,
        program,
        shader_code,
        program->entry_point(),
        convert_vulkan_shader_stage(shader_type),
        shader_type);
    if (vulkan_shader == nullptr) {
        return nullptr;
    }

    return vulkan_shader;
}

void VulkanShader::build_stage_bindings_from_program() {
    variables_.clear();
    push_constants_.clear();
    if (program() == nullptr) {
        return;
    }

    const uint32_t stage_flag = static_cast<uint32_t>(stage_);
    for (const ShaderVariableBinding& binding : program()->variables()) {
        if ((binding.stage_flags & stage_flag) == 0) {
            continue;
        }

        ShaderVariableBinding variable = binding;
        if (variable.is_bindless) {
            variable.count = get_vulkan_bindless_resource_max_count(variable.type);
        }
        variables_.push_back(std::move(variable));
    }

    for (const ShaderPushConstant& push_constant : program()->push_constants()) {
        if ((push_constant.stage_flags & stage_flag) == 0) {
            continue;
        }
        push_constants_.push_back(push_constant);
    }
}

void VulkanShader::create_vertex_stream_binding() {
    const size_t attr_count = get_vertex_attribute_count();
    vertex_stream_binding_.attribute_descriptions_.resize(attr_count);
    vertex_stream_binding_.binding_descriptions_.resize(attr_count);
    vertex_stream_binding_.offsets_.resize(attr_count);
    vertex_stream_binding_.attribute_types_.resize(attr_count);

    std::vector<size_t> attribute_order(attr_count);
    std::iota(attribute_order.begin(), attribute_order.end(), size_t{0});
    std::sort(attribute_order.begin(), attribute_order.end(), [this](size_t lhs, size_t rhs) {
        return get_vertex_attribute(static_cast<uint32_t>(lhs)).location
            < get_vertex_attribute(static_cast<uint32_t>(rhs)).location;
    });

    for (size_t i = 0; i < attr_count; ++i) {
        const VertexAttribute attr = get_vertex_attribute(static_cast<uint32_t>(attribute_order[i]));
        const VkFormat vk_format = vertex_format_to_vulkan(static_cast<VertexFormat>(attr.format));
        vertex_stream_binding_.attribute_descriptions_[i].binding = static_cast<uint32_t>(i);
        vertex_stream_binding_.attribute_descriptions_[i].location = attr.location;
        vertex_stream_binding_.attribute_descriptions_[i].format = vk_format;
        vertex_stream_binding_.attribute_descriptions_[i].offset = 0;
        vertex_stream_binding_.attribute_types_[i] = static_cast<VertexAttributeType::Enum>(attr.type);
        vertex_stream_binding_.offsets_[i] = 0;

        vertex_stream_binding_.binding_descriptions_[i].binding = static_cast<uint32_t>(i);
        vertex_stream_binding_.binding_descriptions_[i].stride = get_vulkan_format_size(vk_format);
        vertex_stream_binding_.binding_descriptions_[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }
}

VulkanShader* VulkanShaderManager::get_or_create_shader_from_program(
    VulkanDevice* device,
    ShaderProgram* program,
    ShaderType shader_type) {
    if (program == nullptr) {
        return nullptr;
    }

    const ProgramShaderKey key{program, shader_type};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = program_shaders_.find(key);
        if (it != program_shaders_.end()) {
            return it->second;
        }
    }

    VulkanShader* shader = VulkanShader::create_for_program(device, program, shader_type);
    if (shader == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = program_shaders_.find(key);
    if (it != program_shaders_.end()) {
        ocarina::delete_with_allocator(shader);
        return it->second;
    }

    const handle_ty shader_handle = reinterpret_cast<handle_ty>(shader);
    shaders_.insert({shader_handle, shader});
    program_shaders_.insert({key, shader});
    vulkan_shader_entries_.insert(std::make_pair(
        shader_handle,
        VulkanShaderEntry{shader->shader_module(), shader->stage(), shader->get_entry_point()}));
    return shader;
}

VulkanShader* VulkanShaderManager::find_shader_from_program(
    ShaderProgram* program,
    ShaderType shader_type) const {
    if (program == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = program_shaders_.find(ProgramShaderKey{program, shader_type});
    return it != program_shaders_.end() ? it->second : nullptr;
}

void VulkanShaderManager::release_program_shaders(ShaderProgram* program) {
    if (program == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (ShaderType stage : {ShaderType::VertexShader, ShaderType::PixelShader, ShaderType::ComputeShader}) {
        const ProgramShaderKey key{program, stage};
        const auto it = program_shaders_.find(key);
        if (it == program_shaders_.end()) {
            continue;
        }
        VulkanShader* shader = it->second;
        shaders_.erase(reinterpret_cast<handle_ty>(shader));
        vulkan_shader_entries_.erase(reinterpret_cast<handle_ty>(shader));
        program_shaders_.erase(it);
        ocarina::delete_with_allocator(shader);
    }
}

VulkanShaderEntry VulkanShaderManager::get_shader_entry(handle_ty shader_handle) const {
    auto it = vulkan_shader_entries_.find(shader_handle);
    if (it != vulkan_shader_entries_.end()) {
        return it->second;
    }
    return {};
}

void VulkanShaderManager::clear(VulkanDevice* device) {
    (void)device;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, shader] : program_shaders_) {
        ocarina::delete_with_allocator(shader);
    }
    program_shaders_.clear();
    vulkan_shader_entries_.clear();
    shaders_.clear();
}

} // namespace ocarina
