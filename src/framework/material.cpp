#include "material.h"
#include "core/hash.h"
#include "bindless_texture_registry.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/renderpass.h"
#include "rhi/shader_base.h"
#include "framework/frame_resources.h"

namespace ocarina {

static std::vector<uint64_t> collect_binding_name_ids(DescriptorSetLayout* layout) {
    std::vector<uint64_t> binding_name_ids;
    if (layout == nullptr) {
        return binding_name_ids;
    }
    const size_t bindings_count = layout->get_bindings_count();
    binding_name_ids.reserve(bindings_count);
    for (size_t i = 0; i < bindings_count; ++i) {
        binding_name_ids.push_back(layout->get_binding_name_id(i));
    }
    return binding_name_ids;
}

uint32_t Material::find_bindless_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        DescriptorSetLayout* layout = layouts[set_index];
        if (layout != nullptr && layout->has_bindless_binding()) {
            return layout->get_descriptor_set_index();
        }
    }
    return InvalidUI32;
}

uint32_t Material::find_global_ubo_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        DescriptorSetLayout* layout = layouts[set_index];
        if (layout == nullptr) {
            continue;
        }
        if (layout->get_descriptor_set_index() != static_cast<uint32_t>(DescriptorSetIndex::GLOBAL_SET)) {
            continue;
        }
        if (!layout->has_bindless_binding() && layout->has_uniform_buffer_binding()) {
            return layout->get_descriptor_set_index();
        }
    }
    return InvalidUI32;
}

uint32_t Material::find_max_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    uint32_t max_index = 0;
    bool any = false;
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        if (layouts[set_index] != nullptr) {
            max_index = static_cast<uint32_t>(set_index);
            any = true;
        }
    }
    return any ? max_index : InvalidUI32;
}

Material::Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) : device_(device) {
    pipeline_state_.shaders[0] = vertex_shader;
    pipeline_state_.shaders[1] = pixel_shader;
    pipeline_state_.descriptorset_layout = InvalidUI64;
    pipeline_state_.raster_state = RasterState::Default();
    pipeline_state_.blend_state = BlendState::Opaque();
    pipeline_state_.depth_stencil_state = DepthStencilState::Default();
    pipeline_state_.primitive_type = PrimitiveType::TRIANGLES;

    void* shaders[2] = {
        reinterpret_cast<void*>(vertex_shader),
        reinterpret_cast<void*>(pixel_shader)
    };
    descriptor_set_layouts_ = device_->create_descriptor_set_layout(shaders, 2);
    init_material_properties(pixel_shader);
    create_global_descriptor_sets();
}

void Material::init_material_properties(handle_ty pixel_shader) {
    const RHIShader* shader = reinterpret_cast<const RHIShader*>(pixel_shader);
    if (shader == nullptr) {
        return;
    }

    std::vector<RHIShader::UniformBufferMember> members;
    uint32_t buffer_size = 0;
    if (!shader->get_uniform_buffer_members(kMaterialUniformBufferName, members, buffer_size)) {
        return;
    }

    material_uniform_buffer_name_id_ = hash64(kMaterialUniformBufferName);
    material_uniform_buffer_size_ = buffer_size;
    material_properties_.reserve(members.size());
    material_property_indices_.clear();

    for (const RHIShader::UniformBufferMember& member : members) {
        MaterialProperty property;
        property.name = member.name;
        property.type = member.type;
        property.size = member.size;
        property.offset = member.offset;
        material_property_indices_.emplace(hash64(property.name), material_properties_.size());
        material_properties_.push_back(std::move(property));
    }
}

const Material::MaterialProperty* Material::find_material_property(uint64_t name_id) const noexcept {
    const auto it = material_property_indices_.find(name_id);
    if (it == material_property_indices_.end()) {
        return nullptr;
    }
    return &material_properties_[it->second];
}

void Material::create_global_descriptor_sets() {
    FrameResources& frame_resources = FrameResources::instance();

    const uint32_t global_ubo_set_index = find_global_ubo_descriptor_set_index(descriptor_set_layouts_);
    if (global_ubo_set_index != InvalidUI32) {
        DescriptorSetLayout* global_layout = descriptor_set_layouts_[global_ubo_set_index];
        std::vector<uint64_t> binding_name_ids = collect_binding_name_ids(global_layout);
        if (!binding_name_ids.empty()) {
            frame_resources.get_or_create_global_descriptor_set(
                global_ubo_set_index,
                binding_name_ids,
                [global_layout]() { return global_layout->allocate_descriptor_set(); });
        }
    }

    const uint32_t bindless_set_index = find_bindless_descriptor_set_index(descriptor_set_layouts_);
    if (bindless_set_index != InvalidUI32) {
        DescriptorSetLayout* bindless_layout = descriptor_set_layouts_[bindless_set_index];
        std::vector<uint64_t> binding_name_ids = collect_binding_name_ids(bindless_layout);
        if (!binding_name_ids.empty()) {
            frame_resources.get_or_create_bindless_descriptor_set(
                bindless_set_index,
                binding_name_ids,
                [bindless_layout]() { return bindless_layout->allocate_descriptor_set(); });
        }
    }
}

void Material::set_target_render_pass(RHIRenderPass* render_pass) {
    if (render_pass_ != render_pass) {
        render_pass_ = render_pass;
        pipeline_dirty_ = true;
    }
    build_pipeline();
}

void Material::build_pipeline() {
    if (!pipeline_dirty_ || render_pass_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    pipeline_ = device_->get_pipeline(pipeline_state_, render_pass_);
    pipeline_dirty_ = false;
}

void Material::try_build_pipeline() {
    if (render_pass_ != nullptr) {
        build_pipeline();
    }
}

}// namespace ocarina
