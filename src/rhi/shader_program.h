#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"
#include "pipeline_state.h"
#include "shader_program_key.h"
#include "shader_reflection.h"

namespace ocarina {

class DescriptorSetLayout;
class Device;
class RHIShader;
class CommandBuffer;
class DescriptorSet;
struct RHIPipeline;
struct RHIPipelineLayout;

/// Backend-agnostic descriptor binding type derived from shader reflection.
enum class ShaderBindingType : uint8_t {
    UniformBuffer = 0,
    SampledImage,
    CombinedImageSampler,
    StorageImage,
    Sampler,
    StorageBuffer,
};

/// One reflected shader resource binding (UBO, texture, sampler, etc.).
struct ShaderVariableBinding {
    char name[256] = {};
    uint8_t binding = 0;
    uint8_t descriptor_set = 0;
    ShaderBindingType type = ShaderBindingType::UniformBuffer;
    uint32_t stage_flags = 0;
    uint32_t count = 1;
    uint32_t size = 0;
    uint32_t array_size = 0;
    bool is_bindless = false;
    std::vector<ShaderReflection::ShaderVariable> members;
};

/// Reflected push-constant block from the shader.
struct ShaderPushConstant {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t stage_flags = 0;
    std::vector<ShaderReflection::ShaderVariable> shader_variables;

    bool operator==(const ShaderPushConstant& other) const {
        return name == other.name && offset == other.offset && size == other.size
            && shader_variables == other.shader_variables;
    }

    bool operator!=(const ShaderPushConstant& other) const { return !(*this == other); }
};

/// CPU-side compiled shader program (VS+PS or CS): merged reflection and descriptor layouts.
class OC_RHI_API ShaderProgram {
public:
    struct UniformBufferMember {
        string name;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    ShaderProgram() = default;

    [[nodiscard]] const ShaderProgramKey& key() const noexcept { return key_; }
    [[nodiscard]] bool is_graphics() const noexcept { return key_.is_graphics(); }
    [[nodiscard]] bool is_compute() const noexcept { return key_.is_compute(); }

    [[nodiscard]] const std::string& vertex_shader_file() const noexcept {
        return key_.vertex_shader_file;
    }
    [[nodiscard]] const std::string& pixel_shader_file() const noexcept {
        return key_.pixel_shader_file;
    }
    [[nodiscard]] const std::string& compute_shader_file() const noexcept {
        return key_.compute_shader_file;
    }
    [[nodiscard]] const std::string& entry_point() const noexcept { return key_.entry_point; }

    [[nodiscard]] const std::vector<uint32_t>& vertex_spirv() const noexcept { return vertex_spirv_; }
    [[nodiscard]] const std::vector<uint32_t>& pixel_spirv() const noexcept { return pixel_spirv_; }
    [[nodiscard]] const std::vector<uint32_t>& compute_spirv() const noexcept { return compute_spirv_; }

    [[nodiscard]] const std::vector<ShaderVariableBinding>& variables() const noexcept {
        return variables_;
    }
    [[nodiscard]] const std::vector<ShaderPushConstant>& push_constants() const noexcept {
        return push_constants_;
    }
    [[nodiscard]] const std::vector<ShaderReflection::UniformBuffer>& named_structs() const noexcept {
        return named_structs_;
    }
    [[nodiscard]] const std::vector<VertexAttribute>& vertex_attributes() const noexcept {
        return vertex_attributes_;
    }

    [[nodiscard]] RHIShader* vertex_shader() const noexcept { return vertex_shader_; }
    [[nodiscard]] RHIShader* pixel_shader() const noexcept { return pixel_shader_; }
    [[nodiscard]] RHIShader* compute_shader() const noexcept { return compute_shader_; }

    [[nodiscard]] uint32_t thread_group_size_x() const noexcept { return thread_group_size_[0]; }
    [[nodiscard]] uint32_t thread_group_size_y() const noexcept { return thread_group_size_[1]; }
    [[nodiscard]] uint32_t thread_group_size_z() const noexcept { return thread_group_size_[2]; }

    [[nodiscard]] RHIPipeline* compute_pipeline() const noexcept { return compute_pipeline_; }
    [[nodiscard]] RHIPipelineLayout* compute_pipeline_layout() const noexcept {
        return compute_pipeline_layout_;
    }

    [[nodiscard]] handle_ty shader_handle(ShaderType stage) const noexcept;

    /// Compile VS+PS from HLSL, merge reflection, and build binding tables.
    [[nodiscard]] static ShaderProgram* compile_graphics_from_HLSL(
        const std::string& vertex_shader_file,
        const std::string& pixel_shader_file,
        const std::set<std::string>& vertex_options,
        const std::set<std::string>& pixel_options,
        const std::string& entry_point = "main");

    /// Compile a single compute shader from HLSL.
    [[nodiscard]] static ShaderProgram* compile_compute_from_HLSL(
        const std::string& compute_shader_file,
        const std::set<std::string>& options,
        const std::string& entry_point = "main");

    void create_descriptor_set_layouts(Device* device);
    void create_descriptor_set_layouts(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);

    [[nodiscard]] const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>&
    descriptor_set_layouts() const noexcept {
        return descriptor_set_layouts_;
    }

    [[nodiscard]] bool has_descriptor_set_layouts() const noexcept {
        return descriptor_set_layouts_ready_;
    }

    void ensure_gpu_shaders(Device* device);

    /// Create compute pipeline layout + PSO (compute programs only).
    void ensure_compute_pipeline(Device* device);

    /// Bind compute pipeline, optional descriptor sets, and dispatch workgroups.
    void dispatch(
        CommandBuffer& cmd,
        DescriptorSet** descriptor_sets,
        uint32_t first_set,
        uint32_t descriptor_set_count,
        uint32_t group_count_x,
        uint32_t group_count_y = 1,
        uint32_t group_count_z = 1);

    /// Dispatch enough workgroups to cover a 2D (or 3D) extent using thread_group_size.
    void dispatch_for_extent(
        CommandBuffer& cmd,
        DescriptorSet** descriptor_sets,
        uint32_t first_set,
        uint32_t descriptor_set_count,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1);

    /// Release compute PSO / layout owned by this program.
    void release_compute_pipeline(Device* device);

    void clear_compute_pipeline() noexcept {
        compute_pipeline_ = nullptr;
        compute_pipeline_layout_ = nullptr;
    }

    void set_vertex_shader(RHIShader* shader) noexcept { vertex_shader_ = shader; }
    void set_pixel_shader(RHIShader* shader) noexcept { pixel_shader_ = shader; }
    void set_compute_shader(RHIShader* shader) noexcept { compute_shader_ = shader; }

    [[nodiscard]] bool get_uniform_buffer_members(
        const char* buffer_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& buffer_size) const;

    [[nodiscard]] bool get_struct_members(
        const char* struct_name,
        std::vector<UniformBufferMember>& members,
        uint32_t& struct_size) const;

    [[nodiscard]] bool has_descriptor_binding(const char* binding_name) const;

    [[nodiscard]] bool get_shader_vertex_inputs(
        VertexInputAttributeDescription* out_attributes,
        uint32_t* inout_attribute_count,
        VertexInputBindingDescription* out_bindings,
        uint32_t* inout_binding_count) const;

    void collect_push_constant_ranges(std::vector<PushConstantRange>& ranges) const;

private:
    void merge_stage_reflection(
        const ShaderReflection& reflection,
        ShaderType shader_type,
        uint32_t stage_flags);
    void finalize_merged_reflection();
    void build_vertex_attributes(const ShaderReflection& reflection);

    ShaderProgramKey key_{};
    std::vector<uint32_t> vertex_spirv_;
    std::vector<uint32_t> pixel_spirv_;
    std::vector<uint32_t> compute_spirv_;
    std::vector<ShaderVariableBinding> variables_;
    std::vector<ShaderPushConstant> push_constants_;
    std::vector<ShaderReflection::UniformBuffer> named_structs_;
    std::vector<VertexAttribute> vertex_attributes_;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    bool descriptor_set_layouts_ready_ = false;

    RHIShader* vertex_shader_ = nullptr;
    RHIShader* pixel_shader_ = nullptr;
    RHIShader* compute_shader_ = nullptr;
    RHIPipeline* compute_pipeline_ = nullptr;
    RHIPipelineLayout* compute_pipeline_layout_ = nullptr;
    uint32_t thread_group_size_[3] = {1, 1, 1};
};

} // namespace ocarina
