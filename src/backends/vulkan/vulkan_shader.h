//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "core/util.h"
#include "rhi/device.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "rhi/shader_program_key.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <mutex>

namespace ocarina {

class VulkanDevice;
class VulkanDescriptorSetLayout;
class DescriptorSetLayout;

struct VulkanVertexStreamBinding {
    std::vector<VertexAttributeType::Enum> attribute_types_;
    std::vector<VkDeviceSize> offsets_;
    std::vector<VkVertexInputBindingDescription> binding_descriptions_;
    std::vector<VkVertexInputAttributeDescription> attribute_descriptions_;
};

/// GPU-side Vulkan shader module linked to a parent ShaderProgram.
class VulkanShader : public RHIShader {
public:
    VulkanShader(
        VulkanDevice* device,
        ShaderProgram* program,
        std::span<uint32_t> shader_code,
        const std::string_view& entry_point,
        VkShaderStageFlagBits stage,
        ShaderType shader_type);
    ~VulkanShader() override;

    [[nodiscard]] size_t get_vertex_attribute_count() const {
        return program() != nullptr ? program()->vertex_attributes().size() : 0;
    }

    [[nodiscard]] VertexAttribute get_vertex_attribute(uint32_t index) const {
        if (program() != nullptr && index < program()->vertex_attributes().size()) {
            return program()->vertex_attributes()[index];
        }
        return VertexAttribute();
    }

    OC_MAKE_MEMBER_GETTER(shader_module, );
    OC_MAKE_MEMBER_GETTER(stage, );

    [[nodiscard]] const char* get_entry_point() const { return entry_.c_str(); }

    static VulkanShader* create_for_program(
        VulkanDevice* device,
        ShaderProgram* program,
        ShaderType shader_type);

    static VkShaderStageFlagBits convert_vulkan_shader_stage(ShaderType shader_type) {
        switch (shader_type) {
            case ShaderType::VertexShader:
                return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderType::PixelShader:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderType::GeometryShader:
                return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderType::ComputeShader:
                return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderType::MeshShader:
                return VK_SHADER_STAGE_MESH_BIT_EXT;
            default:
                return VK_SHADER_STAGE_VERTEX_BIT;
        }
    }

    [[nodiscard]] const std::vector<ShaderPushConstant>& get_push_constants() const { return push_constants_; }

    [[nodiscard]] uint32_t get_shader_variables_count() const { return static_cast<uint32_t>(variables_.size()); }

    [[nodiscard]] const ShaderVariableBinding& get_shader_variable(size_t index) const {
        return variables_[index];
    }

    [[nodiscard]] const VulkanVertexStreamBinding& get_vertex_stream_binding() const {
        return vertex_stream_binding_;
    }

private:
    void build_stage_bindings_from_program();
    void create_vertex_stream_binding();

    VkShaderModule shader_module_ = VK_NULL_HANDLE;
    std::string entry_;
    VulkanDevice* device_ = nullptr;
    VkShaderStageFlagBits stage_;
    std::vector<ShaderVariableBinding> variables_;
    std::vector<ShaderPushConstant> push_constants_;
    VulkanVertexStreamBinding vertex_stream_binding_;
};

struct VulkanShaderEntry {
    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;
    const char* entry = nullptr;
    bool is_valid() const { return shader_module != VK_NULL_HANDLE; }
};

class VulkanShaderManager : concepts::Noncopyable {
public:
    VulkanShader* get_or_create_shader_from_program(
        VulkanDevice* device,
        ShaderProgram* program,
        ShaderType shader_type);

    [[nodiscard]] VulkanShader* find_shader_from_program(
        ShaderProgram* program,
        ShaderType shader_type) const;

    void release_program_shaders(ShaderProgram* program);

    VulkanShaderEntry get_shader_entry(handle_ty shader_handle) const;
    void clear(VulkanDevice* device);
    [[nodiscard]] VulkanShader* get_shader(handle_ty shader_handle) const {
        auto it = shaders_.find(shader_handle);
        if (it != shaders_.end()) {
            return it->second;
        }
        return nullptr;
    }

private:
    struct ProgramShaderKey {
        ShaderProgram* program = nullptr;
        ShaderType stage = ShaderType::VertexShader;

        bool operator==(const ProgramShaderKey& other) const {
            return program == other.program && stage == other.stage;
        }
    };

    struct HashProgramShaderKey {
        size_t operator()(const ProgramShaderKey& key) const {
            size_t hash = 0;
            hash_combine(hash, reinterpret_cast<uintptr_t>(key.program));
            hash_combine(hash, *reinterpret_cast<const uint64_t*>(&key.stage));
            return hash;
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<ProgramShaderKey, VulkanShader*, HashProgramShaderKey> program_shaders_;
    std::unordered_map<handle_ty, VulkanShader*> shaders_;
    std::map<handle_ty, VulkanShaderEntry> vulkan_shader_entries_;
};

} // namespace ocarina
