#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/shader_program.h"
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
#include "math/basic_types.h"

namespace ocarina {

class Device;
class DescriptorSet;
class DescriptorSetLayout;
class CommandBuffer;
struct RHIPipeline;

/// CPU/GPU parameter block for a ShaderProgram (typically compute).
/// Owns descriptor sets for set indices > 0 (set 0 is FrameResources / global).
class OC_FRAMEWORK_API ShaderParameters {
public:
    enum class PropertyKind : uint8_t {
        UniformMember = 0,
        UniformBuffer,
        Texture,
        Sampler,
        StorageBuffer,
    };

    struct Property {
        string name;
        PropertyKind kind = PropertyKind::UniformMember;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        ShaderBindingType binding_type = ShaderBindingType::UniformBuffer;
        uint32_t size = 0;
        uint32_t offset = 0;
        uint64_t uniform_buffer_name_id = 0;
        uint8_t binding = 0;
        uint8_t descriptor_set = 0;
    };

    ShaderParameters(Device* device, ShaderProgram* shader_program);
    ~ShaderParameters();

    ShaderParameters(const ShaderParameters&) = delete;
    ShaderParameters& operator=(const ShaderParameters&) = delete;

    [[nodiscard]] ShaderProgram* shader_program() const noexcept { return shader_program_; }

    /// Allocate local descriptor sets / UBO GPU buffers if needed (render thread).
    void ensure_gpu_ready();
    [[nodiscard]] bool is_ready();

    /// Flush dirty owned uniform buffers to GPU and update descriptors.
    void apply_uploads();

    /// Bind all local descriptor sets (set > 0) for the given pipeline layout.
    void bind(CommandBuffer& cmd, const RHIPipeline* pipeline);

    /// Storage buffer binding (StructuredBuffer / RWStructuredBuffer / ByteAddressBuffer).
    void set_buffer(const char* name, handle_ty buffer, uint64_t offset, uint64_t size);
    void set_buffer(uint64_t name_id, handle_ty buffer, uint64_t offset, uint64_t size);

    /// Sampled texture or storage image (RWTexture) — type comes from reflection.
    void set_texture(const char* name, Texture* texture);
    void set_texture(uint64_t name_id, Texture* texture);

    /// Bind an external uniform buffer to a reflected UBO binding.
    void set_uniform_buffer(const char* name, handle_ty buffer, uint32_t offset, uint32_t size);
    void set_uniform_buffer(uint64_t name_id, handle_ty buffer, uint32_t offset, uint32_t size);

    void set_float(const char* name, float value);
    void set_int(const char* name, int32_t value);
    void set_vector(const char* name, const float2& value);
    void set_vector(const char* name, const float3& value);
    void set_vector(const char* name, const float4& value);
    void set_vector(const char* name, const int2& value);
    void set_vector(const char* name, const int3& value);
    void set_vector(const char* name, const int4& value);
    void set_matrix(const char* name, const float4x4& value);

    [[nodiscard]] DescriptorSet* descriptor_set(uint32_t set_index) const noexcept;
    [[nodiscard]] uint32_t local_descriptor_set_count() const noexcept;

    void release_gpu_buffers();

private:
    struct OwnedUniformBuffer {
        uint32_t size = 0;
        TypedBuffer<std::byte> buffer{};
        std::vector<uint8_t> cpu_data{};
        bool descriptor_bound = false;
        bool dirty = false;
    };

    /// Last value written to a descriptor binding; skip GPU update when unchanged.
    struct CachedDescriptorValue {
        handle_ty resource = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    void init_from_reflection();
    void allocate_descriptor_sets();
    void ensure_uniform_buffer_gpus();
    void upload_owned_uniform_buffer(uint64_t name_id, OwnedUniformBuffer& ubo);
    void write_uniform_member(uint64_t name_id, const void* data, size_t size);

    [[nodiscard]] const Property* find_property(uint64_t name_id) const noexcept;
    [[nodiscard]] OwnedUniformBuffer* find_owned_uniform_buffer(uint64_t name_id) noexcept;
    [[nodiscard]] DescriptorSet* find_descriptor_set_for_property(const Property& property) const noexcept;

    /// Returns true if the cached value changed and the descriptor should be rewritten.
    [[nodiscard]] bool cache_descriptor_value(
        uint64_t name_id,
        handle_ty resource,
        uint64_t offset = 0,
        uint64_t size = 0) noexcept;

    Device* device_ = nullptr;
    ShaderProgram* shader_program_ = nullptr;

    std::vector<Property> properties_;
    std::unordered_map<uint64_t, size_t> property_indices_;
    std::unordered_map<uint64_t, OwnedUniformBuffer> uniform_buffers_;
    std::unordered_map<uint64_t, CachedDescriptorValue> cached_descriptor_values_;

    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    std::array<DescriptorSet*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_sets_ = {};
};

}// namespace ocarina
