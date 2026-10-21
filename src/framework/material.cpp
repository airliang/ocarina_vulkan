#include "material.h"
#include "material_asset.h"
#include "core/hash.h"
#include "core/logging.h"
#include "resource_manager.h"
#include "pipeline_manager.h"
#include "pso_request.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "rhi/resources/resource.h"
#include "framework/frame_resources.h"
#include <algorithm>
#include <optional>

namespace ocarina {
namespace {

constexpr uint32_t kFragmentShaderStageFlag = 16u; // VK_SHADER_STAGE_FRAGMENT_BIT

[[nodiscard]] bool descriptor_set_used_in_pixel_shader(
    const ShaderProgram* program,
    uint32_t set_index) noexcept {
    if (program == nullptr) {
        return false;
    }
    for (const ShaderVariableBinding& binding : program->variables()) {
        if (binding.descriptor_set != set_index) {
            continue;
        }
        if ((binding.stage_flags & kFragmentShaderStageFlag) != 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_material_local_descriptor_set_layout(
    const ShaderProgram* program,
    const DescriptorSetLayout* layout) noexcept {
    if (layout == nullptr) {
        return false;
    }
    const uint32_t set_index = layout->get_descriptor_set_index();
    if (set_index == static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET)) {
        return false;
    }
    return descriptor_set_used_in_pixel_shader(program, set_index);
}

void collect_non_global_descriptor_set_layouts(
    const ShaderProgram* program,
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& out_layouts) {
    if (program == nullptr) {
        return;
    }
    for (DescriptorSetLayout* layout : program->descriptor_set_layouts()) {
        if (!is_material_local_descriptor_set_layout(program, layout)) {
            continue;
        }
        const uint32_t set_index = layout->get_descriptor_set_index();
        if (set_index < out_layouts.size()) {
            out_layouts[set_index] = layout;
        }
    }
}

ocarina_vector<RHIShader::UniformBufferMember> to_rhi_uniform_members(
    const ocarina_vector<ShaderProgram::UniformBufferMember>& members) {
    ocarina_vector<RHIShader::UniformBufferMember> out;
    out.reserve(members.size());
    for (const ShaderProgram::UniformBufferMember& member : members) {
        RHIShader::UniformBufferMember converted;
        converted.name = member.name;
        converted.type = member.type;
        converted.size = member.size;
        converted.offset = member.offset;
        out.push_back(std::move(converted));
    }
    return out;
}

}// namespace

Material::Material(Device* device) : device_(device) {}

Material::Material(Device* device, ShaderProgram* shader_program) : device_(device) {
    // Legacy / fullscreen: one Opaque technique until initialize_from_asset replaces bindings.
    if (shader_program != nullptr) {
        PassBinding binding;
        binding.group = PassGroupId::Opaque;
        binding.technique_name = "Default";
        binding.shader_program = shader_program;
        binding.pipeline_state = PipelineState::MakeGraphicsDefault(shader_program);
        binding.pipeline_dirty = true;
        pass_bindings_.push_back(std::move(binding));
        init_from_shader_program(shader_program);
    }
}

Material::~Material() {
    release_gpu_buffers();
}

void Material::init_from_shader_program(ShaderProgram* shader_program) {
    if (shader_program == nullptr) {
        return;
    }
    descriptor_set_layouts_.fill(nullptr);
    collect_non_global_descriptor_set_layouts(shader_program, descriptor_set_layouts_);
    material_descriptor_set_layout_ = nullptr;
    material_descriptor_set_index_ = InvalidUI32;
    // Resolve layout only. VkDescriptorSet allocation happens on the render thread.
    resolve_material_descriptor_layout();
    init_material_properties(shader_program);
}

ShaderProgram* Material::shader_program_for_pass(PassGroupId group_id) const noexcept {
    const PassBinding* binding = find_pass_binding(group_id);
    return binding != nullptr ? binding->shader_program : nullptr;
}

bool Material::collect_and_share_merged_descriptor_layouts() noexcept {
    if (device_ == nullptr) {
        OC_ERROR("Material::collect_and_share_merged_descriptor_layouts requires a device");
        return false;
    }

    struct MergedBinding {
        ShaderVariableBinding binding{};
        ocarina_string source_technique;
    };

    // Per set: binding name → merged entry; binding index → name (conflict detection).
    std::array<ocarina_map<ocarina_string, MergedBinding>, MAX_DESCRIPTOR_SETS_PER_SHADER>
        by_name{};
    std::array<ocarina_map<uint8_t, ocarina_string>, MAX_DESCRIPTOR_SETS_PER_SHADER>
        name_by_index{};

    const uint8_t frame_set = static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET);

    for (const PassBinding& pass : pass_bindings_) {
        ShaderProgram* program = pass.shader_program;
        if (program == nullptr) {
            continue;
        }
        for (const ShaderVariableBinding& var : program->variables()) {
            if (var.descriptor_set == frame_set) {
                continue;
            }
            if (var.name[0] == '\0') {
                continue;
            }
            if (var.descriptor_set >= MAX_DESCRIPTOR_SETS_PER_SHADER) {
                OC_ERROR_FORMAT(
                    "Material technique '{}': binding '{}' uses invalid descriptor set {}",
                    pass.technique_name.c_str(),
                    var.name,
                    static_cast<uint32_t>(var.descriptor_set));
                return false;
            }

            const uint8_t set = var.descriptor_set;
            const ocarina_string name{var.name};

            auto name_it = by_name[set].find(name);
            if (name_it == by_name[set].end()) {
                auto index_it = name_by_index[set].find(var.binding);
                if (index_it != name_by_index[set].end() && index_it->second != name) {
                    const auto& existing = by_name[set].at(index_it->second);
                    OC_ERROR_FORMAT(
                        "Material technique descriptor conflict: set {} binding index {} is "
                        "'{}' (technique '{}') but technique '{}' declares '{}' at that index. "
                        "All techniques in one material must agree on (set, name) → binding.",
                        static_cast<uint32_t>(set),
                        static_cast<uint32_t>(var.binding),
                        index_it->second.c_str(),
                        existing.source_technique.c_str(),
                        pass.technique_name.c_str(),
                        name.c_str());
                    return false;
                }

                MergedBinding merged;
                merged.binding = var;
                merged.source_technique = pass.technique_name;
                name_by_index[set][var.binding] = name;
                by_name[set].emplace(name, std::move(merged));
                continue;
            }

            MergedBinding& merged = name_it->second;
            if (merged.binding.binding != var.binding) {
                OC_ERROR_FORMAT(
                    "Material technique descriptor conflict: set {} binding '{}' is index {} "
                    "in technique '{}' but index {} in technique '{}'. All techniques in one "
                    "material must use the same binding index for the same name.",
                    static_cast<uint32_t>(set),
                    name.c_str(),
                    static_cast<uint32_t>(merged.binding.binding),
                    merged.source_technique.c_str(),
                    static_cast<uint32_t>(var.binding),
                    pass.technique_name.c_str());
                return false;
            }
            if (merged.binding.type != var.type) {
                OC_ERROR_FORMAT(
                    "Material technique descriptor conflict: set {} binding '{}' has type "
                    "mismatch between techniques '{}' and '{}'.",
                    static_cast<uint32_t>(set),
                    name.c_str(),
                    merged.source_technique.c_str(),
                    pass.technique_name.c_str());
                return false;
            }

            merged.binding.stage_flags |= var.stage_flags;
            if (var.size > merged.binding.size) {
                merged.binding.size = var.size;
            }
            if (var.count > merged.binding.count) {
                merged.binding.count = var.count;
            }
            if (var.is_bindless) {
                merged.binding.is_bindless = true;
            }
        }
    }

    for (uint8_t set = 0; set < MAX_DESCRIPTOR_SETS_PER_SHADER; ++set) {
        if (set == frame_set || by_name[set].empty()) {
            continue;
        }

        ocarina_vector<ShaderVariableBinding> union_bindings;
        union_bindings.reserve(by_name[set].size());
        for (auto& [_, merged] : by_name[set]) {
            merged.binding.descriptor_set = set;
            union_bindings.push_back(merged.binding);
        }
        std::sort(
            union_bindings.begin(),
            union_bindings.end(),
            [](const ShaderVariableBinding& a, const ShaderVariableBinding& b) {
                return a.binding < b.binding;
            });

        DescriptorSetLayout* shared = device_->create_or_get_descriptor_set_layout(
            set,
            union_bindings);
        if (shared == nullptr) {
            OC_ERROR_FORMAT(
                "Material: failed to create merged descriptor set layout for set {}",
                static_cast<uint32_t>(set));
            return false;
        }

        for (PassBinding& pass : pass_bindings_) {
            if (pass.shader_program == nullptr) {
                continue;
            }
            pass.shader_program->set_descriptor_set_layout(set, shared);
        }
    }

    return true;
}

ShaderProgram* Material::select_reflection_shader_program() const noexcept {
    ShaderProgram* best = nullptr;
    size_t best_score = 0;
    const uint8_t material_set = static_cast<uint8_t>(DescriptorSetIndex::MATERIAL_SET);

    for (const PassBinding& pass : pass_bindings_) {
        ShaderProgram* program = pass.shader_program;
        if (program == nullptr) {
            continue;
        }
        size_t score = 0;
        for (const ShaderVariableBinding& var : program->variables()) {
            if (var.descriptor_set == material_set && var.name[0] != '\0') {
                ++score;
                score += var.members.size();
            }
        }
        if (best == nullptr || score > best_score) {
            best = program;
            best_score = score;
        }
    }

    if (best != nullptr) {
        return best;
    }
    return pass_bindings_.empty() ? nullptr : pass_bindings_.front().shader_program;
}

Material::PassBinding* Material::find_pass_binding(PassGroupId group_id) noexcept {
    for (PassBinding& binding : pass_bindings_) {
        if (binding.group == group_id) {
            return &binding;
        }
    }
    return nullptr;
}

const Material::PassBinding* Material::find_pass_binding(PassGroupId group_id) const noexcept {
    for (const PassBinding& binding : pass_bindings_) {
        if (binding.group == group_id) {
            return &binding;
        }
    }
    return nullptr;
}

void Material::set_blend_state(PassGroupId group_id, const BlendState& blend_state) {
    PassBinding* binding = find_pass_binding(group_id);
    if (binding == nullptr || blend_state == binding->pipeline_state.blend_state) {
        return;
    }
    binding->pipeline_state.blend_state = blend_state;
    binding->pipeline_dirty = true;
}

const BlendState& Material::get_blend_state(PassGroupId group_id) const {
    const PassBinding* binding = find_pass_binding(group_id);
    static const BlendState kDefault = BlendState::Opaque();
    return binding != nullptr ? binding->pipeline_state.blend_state : kDefault;
}

void Material::set_alpha_blend_enabled(PassGroupId group_id, bool enabled) {
    PassBinding* binding = find_pass_binding(group_id);
    if (binding == nullptr) {
        return;
    }
    if (enabled) {
        set_blend_state(group_id, BlendState::AlphaBlend());
        DepthStencilState depth_state = binding->pipeline_state.depth_stencil_state;
        depth_state.depth_write_enable = false;
        set_depth_stencil_state(group_id, depth_state);
    } else {
        set_blend_state(group_id, BlendState::Opaque());
        DepthStencilState depth_state = binding->pipeline_state.depth_stencil_state;
        depth_state.depth_write_enable = true;
        set_depth_stencil_state(group_id, depth_state);
    }
}

void Material::set_raster_state(PassGroupId group_id, const RasterState& raster_state) {
    PassBinding* binding = find_pass_binding(group_id);
    if (binding == nullptr || raster_state == binding->pipeline_state.raster_state) {
        return;
    }
    binding->pipeline_state.raster_state = raster_state;
    binding->pipeline_dirty = true;
}

const RasterState& Material::get_raster_state(PassGroupId group_id) const {
    const PassBinding* binding = find_pass_binding(group_id);
    static const RasterState kDefault = RasterState::Default();
    return binding != nullptr ? binding->pipeline_state.raster_state : kDefault;
}

void Material::set_depth_stencil_state(
    PassGroupId group_id,
    const DepthStencilState& depth_stencil_state) {
    PassBinding* binding = find_pass_binding(group_id);
    if (binding == nullptr || depth_stencil_state == binding->pipeline_state.depth_stencil_state) {
        return;
    }
    binding->pipeline_state.depth_stencil_state = depth_stencil_state;
    binding->pipeline_dirty = true;
}

const DepthStencilState& Material::get_depth_stencil_state(PassGroupId group_id) const {
    const PassBinding* binding = find_pass_binding(group_id);
    static const DepthStencilState kDefault = DepthStencilState::Default();
    return binding != nullptr ? binding->pipeline_state.depth_stencil_state : kDefault;
}

void Material::mark_pipeline_dirty() {
    for (PassBinding& binding : pass_bindings_) {
        binding.pipeline_dirty = true;
    }
}

bool Material::is_transparent() const noexcept {
    return find_pass_binding(PassGroupId::Transparent) != nullptr;
}

bool Material::participates_in_pass(PassGroupId group_id) const noexcept {
    return find_pass_binding(group_id) != nullptr;
}

const PipelineState* Material::pipeline_state_for_pass(PassGroupId group_id) const noexcept {
    const PassBinding* binding = find_pass_binding(group_id);
    if (binding == nullptr) {
        return nullptr;
    }
    const_cast<Material*>(this)->ensure_gpu_shaders();
    if (binding->shader_program != nullptr) {
        binding->shader_program->ensure_gpu_shaders(device_);
        const_cast<PassBinding*>(binding)->pipeline_state.shader_program = binding->shader_program;
    }
    return &binding->pipeline_state;
}

bool Material::is_pass_pipeline_dirty(PassGroupId group_id) const noexcept {
    const PassBinding* binding = find_pass_binding(group_id);
    return binding != nullptr && binding->pipeline_dirty;
}

void Material::clear_pass_pipeline_dirty(PassGroupId group_id) noexcept {
    if (PassBinding* binding = find_pass_binding(group_id)) {
        binding->pipeline_dirty = false;
    }
}


bool Material::initialize_from_asset(
    const MaterialAsset& asset,
    LoadingProgressListener* progress_listener) {
    if (device_ == nullptr || !asset.valid()) {
        OC_ERROR("Material::initialize_from_asset requires device and a valid asset");
        return false;
    }

    if (asset.primary_technique() == nullptr) {
        OC_ERROR("Material::initialize_from_asset: asset has no techniques");
        return false;
    }

    // Replace any prior bindings with asset pass_participation entries.
    pass_bindings_.clear();
    pass_bindings_.reserve(asset.pass_participation.size());

    ResourceManager& resources = ResourceManager::instance();

    for (const auto& [pass_name, technique_name] : asset.pass_participation) {
        const std::optional<PassGroupId> group = pass_group_id_from_name(pass_name);
        if (!group) {
            OC_WARNING_FORMAT(
                "Material::initialize_from_asset: unknown pass '{}' in pass_participation",
                pass_name.c_str());
            continue;
        }
        const MaterialTechniqueDesc* technique = asset.find_technique(technique_name);
        if (technique == nullptr
            || technique->vertex_shader.empty()
            || technique->pixel_shader.empty()) {
            OC_WARNING_FORMAT(
                "Material::initialize_from_asset: missing technique '{}' for pass '{}'",
                technique_name.c_str(),
                pass_name.c_str());
            continue;
        }

        PassBinding binding;
        binding.group = *group;
        binding.technique_name = technique_name;
        binding.render_pass = technique->render_pass;
        binding.shader_program = resources.create_shader_program(
            device_,
            technique->vertex_shader,
            technique->pixel_shader,
            {},
            asset.technique_pixel_options(*technique));
        if (binding.shader_program == nullptr) {
            continue;
        }
        binding.shader_program->ensure_gpu_shaders(device_);

        binding.pipeline_state = PipelineState::MakeGraphicsDefault(binding.shader_program);
        asset.apply_technique_overrides_to_pipeline(*technique, binding.pipeline_state);
        binding.pipeline_dirty = true;
        pass_bindings_.push_back(std::move(binding));
    }

    if (pass_bindings_.empty()) {
        OC_ERROR("Material::initialize_from_asset: no valid pass bindings");
        return false;
    }

    // Union MATERIAL_SET (and other non-FRAME sets) across all techniques; error on
    // (set, name) -> binding index conflicts. Shared layout is assigned to every program.
    if (!collect_and_share_merged_descriptor_layouts()) {
        return false;
    }

    ShaderProgram* reflection_program = select_reflection_shader_program();
    if (reflection_program == nullptr) {
        OC_ERROR("Material::initialize_from_asset: no ShaderProgram available for reflection");
        return false;
    }
    init_from_shader_program(reflection_program);

    for (const MaterialPropertyDesc& property : asset.properties) {
        if (!property.has_value) {
            continue;
        }
        switch (property.type) {
            case MaterialPropertyValueType::Float:
                set_property(property.name.c_str(), property.float_value);
                break;
            case MaterialPropertyValueType::Float4:
                set_property(property.name.c_str(), property.float4_value);
                break;
            case MaterialPropertyValueType::Texture2D:
                // Texture paths are applied by the loader after GPU texture creation.
                break;
        }
    }

    // Enqueue a PipelineCompileTask per bound pass that has an RHIRenderPass.
    for (PassBinding& binding : pass_bindings_) {
        if (binding.render_pass == nullptr || binding.shader_program == nullptr) {
            continue;
        }
        PSORequest request = PSORequest::from_pipeline_state(
            binding.pipeline_state,
            binding.render_pass);
        request.shader_program = binding.shader_program;
        PipelineManager::instance().enqueue(std::move(request), progress_listener);
    }

    return true;
}


void Material::release_gpu_buffers() {
    ResourceManager& resources = ResourceManager::instance();
    for (auto& [name_id, ubo] : uniform_buffers_) {
        (void)name_id;
        if (ubo.buffer.handle() != 0) {
            resources.release_buffer(ubo.buffer.handle());
            ubo.buffer.reset();
        }
        ubo.descriptor_bound = false;
        ubo.dirty = false;
    }
}

void Material::apply_reflected_members(
    uint64_t buffer_name_id,
    uint32_t buffer_size,
    const ocarina_vector<RHIShader::UniformBufferMember>& members) {
    (void)buffer_size;
    for (const RHIShader::UniformBufferMember& member : members) {
        MaterialProperty property;
        property.name = member.name;
        property.kind = PropertyKind::UniformMember;
        property.type = member.type;
        property.binding_type = ShaderBindingType::UniformBuffer;
        property.size = member.size;
        property.offset = member.offset;
        property.uniform_buffer_name_id = buffer_name_id;
        material_property_indices_.emplace(hash64(property.name), material_properties_.size());
        material_properties_.push_back(std::move(property));
    }
}

void Material::add_binding_property(
    const ShaderVariableBinding& binding,
    PropertyKind kind) {
    if (binding.name[0] == '\0') {
        return;
    }

    const uint64_t name_id = hash64(binding.name);
    if (material_property_indices_.find(name_id) != material_property_indices_.end()) {
        return;
    }

    MaterialProperty property;
    property.name = binding.name;
    property.kind = kind;
    property.binding_type = binding.type;
    property.size = binding.size;
    property.binding = binding.binding;
    property.descriptor_set = binding.descriptor_set;
    if (kind == PropertyKind::UniformBuffer || kind == PropertyKind::StorageBuffer) {
        property.uniform_buffer_name_id = name_id;
    }
    material_property_indices_.emplace(name_id, material_properties_.size());
    material_properties_.push_back(std::move(property));
}

void Material::add_uniform_buffer_property(
    const char* binding_name,
    uint32_t buffer_size,
    const ocarina_vector<RHIShader::UniformBufferMember>& members,
    bool create_owned_buffer,
    uint8_t binding,
    uint8_t descriptor_set) {
    if (binding_name == nullptr || binding_name[0] == '\0' || buffer_size == 0) {
        return;
    }

    const uint64_t name_id = hash64(binding_name);
    if (material_property_indices_.find(name_id) != material_property_indices_.end()) {
        return;
    }

    MaterialProperty ubo_property;
    ubo_property.name = binding_name;
    ubo_property.kind = PropertyKind::UniformBuffer;
    ubo_property.binding_type = ShaderBindingType::UniformBuffer;
    ubo_property.size = buffer_size;
    ubo_property.uniform_buffer_name_id = name_id;
    ubo_property.binding = binding;
    ubo_property.descriptor_set = descriptor_set;
    material_property_indices_.emplace(name_id, material_properties_.size());
    material_properties_.push_back(std::move(ubo_property));

    if (material_params_buffer_name_id_ == 0) {
        material_params_buffer_name_id_ = name_id;
        material_params_byte_size_ = buffer_size;
    }

    apply_reflected_members(name_id, buffer_size, members);

    if (create_owned_buffer) {
        OwnedUniformBuffer owned;
        owned.size = buffer_size;
        owned.cpu_data.assign(buffer_size, 0);
        owned.dirty = true;
        uniform_buffers_.emplace(name_id, std::move(owned));
    }
}

void Material::ensure_gpu_shaders() {
    if (device_ == nullptr) {
        return;
    }

    for (PassBinding& binding : pass_bindings_) {
        if (binding.shader_program == nullptr) {
            continue;
        }
        binding.shader_program->ensure_gpu_shaders(device_);
        binding.pipeline_state.shader_program = binding.shader_program;
    }
}

void Material::init_material_properties(ShaderProgram* shader_program) {
    if (shader_program == nullptr) {
        return;
    }

    material_properties_.clear();
    material_property_indices_.clear();
    uniform_buffers_.clear();
    material_params_buffer_name_id_ = 0;
    material_params_byte_size_ = 0;

    ocarina_vector<ShaderProgram::UniformBufferMember> members;
    uint32_t buffer_size = 0;

    for (const ShaderVariableBinding& binding : shader_program->variables()) {
        if (binding.descriptor_set == static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET)) {
            continue;
        }
        if (binding.name[0] == '\0') {
            continue;
        }

        switch (binding.type) {
            case ShaderBindingType::UniformBuffer: {
                if (!shader_program->get_uniform_buffer_members(binding.name, members, buffer_size)
                    || buffer_size == 0) {
                    // Still register the binding even if member reflection failed.
                    add_binding_property(binding, PropertyKind::UniformBuffer);
                    break;
                }
                add_uniform_buffer_property(
                    binding.name,
                    buffer_size,
                    to_rhi_uniform_members(members),
                    true,
                    binding.binding,
                    binding.descriptor_set);
                break;
            }
            case ShaderBindingType::SampledImage:
            case ShaderBindingType::CombinedImageSampler:
            case ShaderBindingType::StorageImage:
                add_binding_property(binding, PropertyKind::Texture);
                break;
            case ShaderBindingType::Sampler:
                add_binding_property(binding, PropertyKind::Sampler);
                break;
            case ShaderBindingType::StorageBuffer:
                add_binding_property(binding, PropertyKind::StorageBuffer);
                break;
            default:
                break;
        }
    }
}

const Material::MaterialProperty* Material::find_material_property(uint64_t name_id) const noexcept {
    const auto it = material_property_indices_.find(name_id);
    if (it == material_property_indices_.end()) {
        return nullptr;
    }
    return &material_properties_[it->second];
}

Material::OwnedUniformBuffer* Material::find_owned_uniform_buffer(uint64_t name_id) noexcept {
    const auto it = uniform_buffers_.find(name_id);
    if (it == uniform_buffers_.end()) {
        return nullptr;
    }
    return &it->second;
}

DescriptorSet* Material::find_descriptor_set_by_property_name(uint64_t name_id) const noexcept {
    (void)name_id;
    return material_descriptor_set_;
}

void Material::resolve_material_descriptor_layout() {
    if (material_descriptor_set_layout_ != nullptr) {
        return;
    }

    for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
        DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
        if (layout == nullptr) {
            continue;
        }
        material_descriptor_set_index_ = static_cast<uint32_t>(set_index);
        material_descriptor_set_layout_ = layout;
        return;
    }
}

void Material::create_material_descriptor_set() {
    if (material_descriptor_set_ != nullptr) {
        return;
    }

    if (material_descriptor_set_layout_ == nullptr) {
        resolve_material_descriptor_layout();
    }

    if (material_descriptor_set_layout_ == nullptr) {
        return;
    }

    material_descriptor_set_ = material_descriptor_set_layout_->allocate_descriptor_set();
}

void Material::try_finish_gpu_init() {
    if (material_descriptor_set_ == nullptr) {
        ShaderProgram* program = select_reflection_shader_program();
        descriptor_set_layouts_.fill(nullptr);
        collect_non_global_descriptor_set_layouts(program, descriptor_set_layouts_);
        material_descriptor_set_layout_ = nullptr;
        resolve_material_descriptor_layout();
        create_material_descriptor_set();
    }

    ensure_uniform_buffer_gpus();
}

bool Material::is_material_infrastructure_ready() const noexcept {
    if (requires_local_descriptor_set_layout() && material_descriptor_set_ == nullptr) {
        return false;
    }

    if (has_material_uniform_buffer()) {
        for (const auto& [name_id, ubo] : uniform_buffers_) {
            (void)name_id;
            if (ubo.size > 0 && ubo.buffer.handle() == 0) {
                return false;
            }
        }
    }

    return true;
}

bool Material::is_GPU_ready() {
    try_finish_gpu_init();
    return is_material_infrastructure_ready();
}

void Material::set_property(uint64_t name_id, const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::UniformMember) {
        return;
    }

    OwnedUniformBuffer* ubo = find_owned_uniform_buffer(property->uniform_buffer_name_id);
    if (ubo == nullptr || ubo->cpu_data.empty()) {
        return;
    }

    const size_t copy_size = std::min(size, static_cast<size_t>(property->size));
    if (property->offset + copy_size > ubo->cpu_data.size()) {
        return;
    }

    bool should_queue = false;
    {
        // The render thread may be uploading this buffer. Publish the bytes and the
        // dirty bit together so that upload cannot clear a write it did not copy.
        std::lock_guard<std::mutex> lock(uniform_mutex_);
        memcpy(ubo->cpu_data.data() + property->offset, data, copy_size);
        ubo->dirty = true;
        if (!in_update_queue_) {
            in_update_queue_ = true;
            should_queue = true;
        }
    }
    if (should_queue) {
        queue_uniform_buffer_update();
    }
}

void Material::set_property(uint64_t name_id, const TextureHandle& texture) {
    if (name_id == 0 || texture.texture_ == nullptr) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->kind != PropertyKind::Texture) {
        return;
    }

    texture_handles_[name_id] = texture;
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_texture(this, name_id, texture));
}

void Material::set_cubemap(uint64_t name_id, Cubemap* cubemap) {
    if (name_id == 0 || cubemap == nullptr) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->kind != PropertyKind::Texture) {
        return;
    }

    cubemap_handles_[name_id] = cubemap;
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_cubemap(this, name_id, cubemap));
}

void Material::set_bindless_texture(uint64_t name_id, const TextureHandle& texture) {
    if (name_id == 0 || texture.texture_ == nullptr) {
        return;
    }
    texture_handles_[name_id] = texture;
}

bool Material::is_renderable() {
    if (!is_GPU_ready()) {
        return false;
    }

    for (const auto& [name_id, handle] : texture_handles_) {
        (void)name_id;
        Texture* bound_texture = handle.texture_;
        if (bound_texture == nullptr ||
            bound_texture->gpu_resource_state() < GPUResourceState::GPU_Visible) {
            return false;
        }
    }
    for (const auto& [name_id, cubemap] : cubemap_handles_) {
        (void)name_id;
        if (cubemap == nullptr ||
            cubemap->gpu_resource_state() < GPUResourceState::GPU_Visible) {
            return false;
        }
    }
    return true;
}

bool Material::requires_local_descriptor_set_layout() const noexcept {
    for (DescriptorSetLayout* layout : descriptor_set_layouts_) {
        if (layout != nullptr) {
            return true;
        }
    }

    ShaderProgram* program = select_reflection_shader_program();
    if (program != nullptr) {
        for (DescriptorSetLayout* layout : program->descriptor_set_layouts()) {
            if (is_material_local_descriptor_set_layout(program, layout)) {
                return true;
            }
        }
    }
    return false;
}

void Material::ensure_uniform_buffer_gpus() {
    if (device_ == nullptr || material_descriptor_set_ == nullptr) {
        return;
    }

    DescriptorSetLayout* layout = material_descriptor_set_layout();
    if (layout == nullptr || !layout->has_uniform_buffer_binding()) {
        return;
    }

    for (auto& [name_id, ubo] : uniform_buffers_) {
        if (ubo.buffer.handle() != 0 || ubo.size == 0) {
            continue;
        }

        const MaterialProperty* property = find_material_property(name_id);
        const char* buffer_name = property != nullptr ? property->name.c_str() : "material_uniform";
        ubo.buffer = ResourceManager::instance().create_buffer<std::byte>(
            device_,
            ubo.size,
            GraphicBufferBindFlags::ConstantBuffer,
            buffer_name);
        ubo.descriptor_bound = false;
        ubo.dirty = true;
    }
}

void Material::queue_uniform_buffer_update() {
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_uniform_buffer(this));
}

bool Material::apply_material_parameters_upload() {
    ensure_uniform_buffer_gpus();

    struct UploadSnapshot {
        uint64_t name_id = 0;
        OwnedUniformBuffer* ubo = nullptr;
        ocarina_vector<uint8_t> bytes;
        bool needs_descriptor_bind = false;
    };
    ocarina_vector<UploadSnapshot> snapshots;

    {
        std::lock_guard<std::mutex> lock(uniform_mutex_);
        for (auto& [name_id, ubo] : uniform_buffers_) {
            if (ubo.size > 0 && ubo.buffer.handle() == 0) {
                // GPU buffer is not ready. Leave in_update_queue_ set and retry.
                return false;
            }
        }
        for (auto& [name_id, ubo] : uniform_buffers_) {
            if (!ubo.dirty && ubo.descriptor_bound) {
                continue;
            }
            UploadSnapshot snapshot;
            snapshot.name_id = name_id;
            snapshot.ubo = &ubo;
            snapshot.bytes = ubo.cpu_data;
            snapshot.needs_descriptor_bind = !ubo.descriptor_bound;
            ubo.dirty = false;
            snapshots.push_back(std::move(snapshot));
        }
    }

    for (UploadSnapshot& snapshot : snapshots) {
        if (snapshot.ubo == nullptr || snapshot.bytes.empty()) {
            continue;
        }
        snapshot.ubo->buffer.copy_from_immediately(snapshot.bytes.data(), static_cast<uint32_t>(snapshot.bytes.size()));
        if (!snapshot.needs_descriptor_bind) {
            continue;
        }
        DescriptorSet* descriptor_set = find_descriptor_set_by_property_name(snapshot.name_id);
        if (descriptor_set == nullptr) {
            continue;
        }
        descriptor_set->update_buffer(
            snapshot.name_id,
            snapshot.ubo->buffer.handle(),
            0,
            snapshot.ubo->size);
        std::lock_guard<std::mutex> lock(uniform_mutex_);
        snapshot.ubo->descriptor_bound = true;
    }

    std::lock_guard<std::mutex> lock(uniform_mutex_);
    for (auto& [name_id, ubo] : uniform_buffers_) {
        (void)name_id;
        if (ubo.dirty || (ubo.size > 0 && !ubo.descriptor_bound)) {
            // set_property ran after the snapshot, or the descriptor bind is still pending.
            return false;
        }
    }
    in_update_queue_ = false;
    return true;
}

void Material::add_sampler(uint64_t name_id, const TextureSampler& sampler) {
    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->kind != PropertyKind::Sampler) {
        return;
    }

    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_sampler(this, name_id, sampler));
}

}// namespace ocarina
