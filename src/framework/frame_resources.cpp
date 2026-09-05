#include "frame_resources.h"
#include "camera.h"
#include "entity_component_system.h"
#include "material.h"
#include "resource_manager.h"
#include "rhi/command_buffer.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/shader_program.h"
#include "rhi/resources/resource.h"
#include "core/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ocarina {

namespace {

constexpr uint32_t kVertexFragmentStageFlags = 1u | 16u; // VS | PS

float3 normalize_or_default(const float3& v, const float3& fallback) noexcept {
    const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq < 1e-12f) {
        return fallback;
    }
    const float inv_len = 1.0f / std::sqrt(len_sq);
    return make_float3(v.x * inv_len, v.y * inv_len, v.z * inv_len);
}

ShaderVariableBinding make_frame_binding(
    const char* name,
    uint8_t binding,
    ShaderBindingType type,
    uint32_t size,
    bool is_bindless) {
    ShaderVariableBinding result{};
    std::strncpy(result.name, name, sizeof(result.name) - 1);
    result.binding = binding;
    result.descriptor_set = static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET);
    result.type = type;
    result.stage_flags = kVertexFragmentStageFlags;
    result.size = size;
    result.count = 1;
    result.is_bindless = is_bindless;
    return result;
}

}// namespace

FrameResources& FrameResources::instance() {
    static FrameResources s_instance;
    return s_instance;
}

void FrameResources::initialize(Device* device) {
    if (device_ != nullptr && device_ != device) {
        release_gpu_buffers();
    }
    device_ = device;
    create_global_descriptor_set();
    create_default_gpu_buffers();
}

void FrameResources::create_global_descriptor_set() {
    if (device_ == nullptr || frame_descriptor_set_layout_ != nullptr) {
        return;
    }

    // Matches res/shaderlibrary/builtin/frame.hlsl / descriptor_bindings.hlsl.
    const ShaderVariableBinding frame_bindings[] = {
        make_frame_binding("global_ubo", 0, ShaderBindingType::UniformBuffer,
            static_cast<uint32_t>(sizeof(GlobalUniformBuffer)), false),
        make_frame_binding("g_textures", 1, ShaderBindingType::SampledImage, 0, true),
        make_frame_binding("g_samplers", 2, ShaderBindingType::Sampler, 0, true),
        make_frame_binding("g_transforms", 3, ShaderBindingType::StorageBuffer, 0, false),
    };

    frame_descriptor_set_layout_ = device_->create_frame_descriptor_set_layout(frame_bindings);
    if (frame_descriptor_set_layout_ == nullptr) {
        return;
    }

    global_descriptor_set_ = frame_descriptor_set_layout_->allocate_descriptor_set();
    if (global_descriptor_set_ == nullptr) {
        return;
    }

    for (const ShaderVariableBinding& binding : frame_bindings) {
        global_descriptor_sets_by_name_[hash64(binding.name)] = global_descriptor_set_;
    }
    global_ubo_descriptor_bound_ = false;
    transform_storage_descriptor_bound_ = false;
}

void FrameResources::release_gpu_buffers() {
    ResourceManager& resources = ResourceManager::instance();

    if (global_ubo_buffer_.handle() != 0) {
        resources.release_buffer(global_ubo_buffer_.handle());
        global_ubo_buffer_.reset();
    }
    global_ubo_descriptor_bound_ = false;

    if (transform_buffer_.handle() != 0) {
        resources.release_buffer(transform_buffer_.handle());
        transform_buffer_.reset();
    }
    transform_storage_descriptor_bound_ = false;

    global_descriptor_sets_by_name_.clear();
    global_descriptor_set_ = nullptr;
    frame_descriptor_set_layout_ = nullptr;

    // Drop the device pointer so later singleton teardown cannot recreate Vulkan objects.
    device_ = nullptr;
}

void FrameResources::ensure_global_descriptor_sets(RHIPipelineLayout* pipeline_layout) {
    if (pipeline_layout == nullptr) {
        return;
    }

    pipeline_layout->global_descriptor_set_count_ = 0;
    if (global_descriptor_set_ == nullptr) {
        return;
    }

    pipeline_layout->global_descriptor_set_indices_[0] =
        static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET);
    pipeline_layout->global_descriptor_set_count_ = 1;
}

void FrameResources::bind_global_descriptor_sets(
    CommandBuffer& cmd,
    RHIPipelineLayout* pipeline_layout) {
    if (pipeline_layout == nullptr
        || pipeline_layout->handle == 0
        || pipeline_layout->handle == InvalidUI64
        || global_descriptor_set_ == nullptr) {
        return;
    }

    ensure_global_descriptor_sets(pipeline_layout);

    DescriptorSet* descriptor_set = global_descriptor_set_;
    cmd.bind_descriptor_sets(
        &descriptor_set,
        static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET),
        1,
        pipeline_layout->handle);
}

DescriptorSet* FrameResources::get_global_descriptor_set(uint64_t name_id) const {
    auto it = global_descriptor_sets_by_name_.find(name_id);
    return it == global_descriptor_sets_by_name_.end() ? nullptr : it->second;
}

DescriptorSet* FrameResources::get_global_descriptor_set(const std::string& name) const {
    return get_global_descriptor_set(hash64(name));
}

void FrameResources::queue_bindless_texture_update(uint32_t index, Texture* texture) {
    if (texture == nullptr || index == InvalidUI32) {
        return;
    }
    pending_bindless_updates_.push(PendingBindlessUpdate{index, texture});
}

void FrameResources::flush_pending_bindless_updates() {
    if (global_descriptor_set_ == nullptr) {
        return;
    }

    PendingBindlessUpdate update;
    while (pending_bindless_updates_.try_pop(update)) {
        if (update.texture != nullptr && update.index != InvalidUI32) {
            global_descriptor_set_->update_bindless_texture_at_index(update.index, update.texture);
            update.texture->set_gpu_resource_state(GPUResourceState::GPU_Visible);
        }
    }
}

void FrameResources::queue_material_update(MaterialUpdateRequest request) {
    if (request.material == nullptr) {
        return;
    }

    switch (request.kind) {
        case MaterialUpdateKind::Texture:
            if (request.name_id == 0 || request.texture_handle.texture_ == nullptr) {
                return;
            }
            break;
        case MaterialUpdateKind::Sampler:
            if (request.name_id == 0) {
                return;
            }
            break;
        case MaterialUpdateKind::UniformBuffer:
            break;
        default:
            return;
    }

    material_update_queue_.push_back(std::move(request));
}

void FrameResources::process_material_update() {
    material_update_queue_.for_each_remove_if([this](MaterialUpdateRequest& request) -> bool {
        if (request.material == nullptr) {
            return true;
        }

        request.material->try_finish_gpu_init();

        switch (request.kind) {
            case MaterialUpdateKind::Texture: {
                Texture* texture = request.texture_handle.texture_;
                if (texture == nullptr || !texture->is_gpu_ready()) {
                    return false;
                }
                if (!request.material->is_material_infrastructure_ready()) {
                    return false;
                }

                DescriptorSet* descriptor_set = request.material->get_material_descriptor_set();
                if (descriptor_set == nullptr) {
                    OC_INFO("[material_update] Texture pending: descriptor set not ready");
                    return false;
                }

                descriptor_set->update_texture(request.name_id, texture);
                texture->set_gpu_resource_state(GPUResourceState::GPU_Visible);
                return true;
            }
            case MaterialUpdateKind::Sampler: {
                if (!request.material->is_material_infrastructure_ready()) {
                    return false;
                }

                DescriptorSet* descriptor_set = request.material->get_material_descriptor_set();
                if (descriptor_set == nullptr) {
                    OC_INFO("[material_update] Sampler pending: descriptor set not ready");
                    return false;
                }

                descriptor_set->update_sampler(request.name_id, request.sampler);
                return true;
            }
            case MaterialUpdateKind::UniformBuffer: {
                if (!request.material->is_material_infrastructure_ready()) {
                    return false;
                }

                request.material->apply_material_parameters_upload();
                request.material->clear_uniform_buffer_update_queued();
                return true;
            }
            default:
                return true;
        }
    });
}

void FrameResources::set_sun_direction(const float3& direction) noexcept {
    const float3 n = normalize_or_default(direction, make_float3(-0.4f, -1.0f, -0.3f));
    global_ubo_.sun_direction = make_float4(n.x, n.y, n.z, 0.0f);
}

void FrameResources::set_sun_color(const float3& color) noexcept {
    global_ubo_.sun_color = make_float4(color.x, color.y, color.z, 1.0f);
}

void FrameResources::set_sun_intensity(float intensity) noexcept {
    global_ubo_.sun_intensity = intensity;
}

void FrameResources::set_light_position(const float3& position) noexcept {
    global_ubo_.light_pos = make_float4(position.x, position.y, position.z, 1.0f);
}

void FrameResources::create_default_gpu_buffers() {
    if (device_ == nullptr) {
        return;
    }

    if (global_ubo_buffer_.handle() == 0) {
        global_ubo_buffer_ = ResourceManager::instance().create_buffer<GlobalUniformBuffer>(
            device_,
            1,
            GraphicBufferBindFlags::ConstantBuffer,
            "global_ubo");
        global_ubo_descriptor_bound_ = false;
    }

    if (transform_buffer_.handle() == 0) {
        transform_buffer_ = ResourceManager::instance().create_buffer<GPUTransform>(
            device_,
            EntityComponentSystem::kDefaultGpuTransformCapacity,
            GraphicBufferBindFlags::StructuredBuffer,
            EntityComponentSystem::kTransformsBufferName);
        transform_storage_descriptor_bound_ = false;
    }
}

void FrameResources::grow_transform_gpu_buffer(size_t element_count) {
    if (device_ == nullptr) {
        return;
    }

    const size_t required = std::max(element_count, size_t{1});
    if (transform_buffer_.handle() != 0 && transform_buffer_.size() >= required) {
        return;
    }

    const size_t grown = std::max(
        required,
        transform_buffer_.size() == 0 ? required : transform_buffer_.size() * 2);

    if (transform_buffer_.handle() != 0) {
        ResourceManager::instance().release_buffer(transform_buffer_.handle());
        transform_buffer_.reset();
    }

    transform_buffer_ = ResourceManager::instance().create_buffer<GPUTransform>(
        device_,
        grown,
        GraphicBufferBindFlags::StructuredBuffer,
        EntityComponentSystem::kTransformsBufferName);
    transform_storage_descriptor_bound_ = false;

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    const size_t used_count = ecs.gpu_transform_count();
    if (transform_buffer_.handle() != 0 && used_count > 0) {
        const size_t copy_count = std::min(used_count, transform_buffer_.size());
        transform_buffer_.copy_from_immediately(
            ecs.gpu_transforms().data(),
            static_cast<uint32_t>(copy_count * sizeof(GPUTransform)));
    }
}

void FrameResources::bind_transform_storage_buffer_if_needed() {
    if (transform_storage_descriptor_bound_ || transform_buffer_.handle() == 0) {
        return;
    }

    if (global_descriptor_set_ == nullptr) {
        return;
    }

    global_descriptor_set_->update_storage_buffer(
        hash64(EntityComponentSystem::kTransformsBufferName),
        transform_buffer_.handle(),
        0,
        transform_buffer_.size_in_byte());
    transform_storage_descriptor_bound_ = true;
}

void FrameResources::upload_transform_buffer() {
    if (global_descriptor_set_ == nullptr || device_ == nullptr) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    if (!ecs.gpu_transforms_dirty() &&
        transform_buffer_.handle() != 0 &&
        transform_storage_descriptor_bound_) {
        return;
    }

    ecs.sync_gpu_transforms();

    const size_t transform_count = ecs.gpu_transform_count();
    if (transform_count > transform_buffer_.size()) {
        grow_transform_gpu_buffer(transform_count);
    }
    bind_transform_storage_buffer_if_needed();

    if (!ecs.gpu_transforms_dirty() || transform_buffer_.handle() == 0) {
        return;
    }

    const size_t upload_count = std::max(transform_count, size_t{1});
    const size_t upload_bytes = upload_count * sizeof(GPUTransform);
    transform_buffer_.copy_from_immediately(
        ecs.gpu_transforms().data(),
        static_cast<uint32_t>(upload_bytes));
    ecs.clear_gpu_transforms_dirty();
}

void FrameResources::bind_global_ubo_if_needed() {
    if (global_ubo_descriptor_bound_ || global_ubo_buffer_.handle() == 0) {
        return;
    }

    if (global_descriptor_set_ == nullptr) {
        return;
    }

    global_descriptor_set_->update_buffer(
        hash64("global_ubo"),
        global_ubo_buffer_.handle(),
        0,
        static_cast<uint32_t>(global_ubo_buffer_.size_in_byte()));
    global_ubo_descriptor_bound_ = true;
}

void FrameResources::upload_global_uniform_buffer(Camera* camera) {
    if (global_descriptor_set_ == nullptr || device_ == nullptr) {
        return;
    }

    if (camera != nullptr) {
        global_ubo_.projection_matrix = camera->get_projection_matrix().transpose();
        global_ubo_.view_matrix = camera->get_view_matrix().transpose();
        const math3d::Vector3D& cam_position = camera->get_position();
        global_ubo_.camera_pos = make_float4(cam_position[0], cam_position[1], cam_position[2], 1.0f);
    }

    set_sun_direction(make_float3(
        global_ubo_.sun_direction.x,
        global_ubo_.sun_direction.y,
        global_ubo_.sun_direction.z));

    bind_global_ubo_if_needed();

    if (global_ubo_buffer_.handle() == 0) {
        return;
    }

    global_ubo_buffer_.copy_from_immediately(
        &global_ubo_,
        static_cast<uint32_t>(sizeof(GlobalUniformBuffer)));
}

void FrameResources::update_per_frame(double dt, Camera* camera) {
    upload_global_uniform_buffer(camera);
    upload_transform_buffer();
    flush_pending_bindless_updates();
    process_material_update();

    if (update_) {
        update_(*this, dt);
    }
}

}// namespace ocarina
