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

float3 normalize_or_default(const float3& v, const float3& fallback) noexcept {
    const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq < 1e-12f) {
        return fallback;
    }
    const float inv_len = 1.0f / std::sqrt(len_sq);
    return make_float3(v.x * inv_len, v.y * inv_len, v.z * inv_len);
}

std::vector<uint64_t> collect_binding_name_ids(const DescriptorSetLayout* layout) {
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

    ShaderVariableBinding global_ubo_binding{};
    std::strncpy(global_ubo_binding.name, "global_ubo", sizeof(global_ubo_binding.name) - 1);
    global_ubo_binding.binding = 0;
    global_ubo_binding.descriptor_set = static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET);
    global_ubo_binding.type = ShaderBindingType::UniformBuffer;
    global_ubo_binding.stage_flags = 1u | 16u; // VS | PS (Vulkan stage bits)
    global_ubo_binding.size = static_cast<uint32_t>(sizeof(GlobalUniformBuffer));
    global_ubo_binding.count = 1;
    global_ubo_binding.is_bindless = false;

    const ShaderVariableBinding frame_bindings[] = { global_ubo_binding };
    frame_descriptor_set_layout_ = device_->create_frame_descriptor_set_layout(frame_bindings);
    if (frame_descriptor_set_layout_ == nullptr) {
        return;
    }

    DescriptorSet* descriptor_set = frame_descriptor_set_layout_->allocate_descriptor_set();
    if (descriptor_set == nullptr) {
        return;
    }

    const uint64_t global_ubo_name_id = hash64("global_ubo");
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    if (global_descriptor_sets_.size() < 1) {
        global_descriptor_sets_.resize(1, nullptr);
    }
    global_descriptor_sets_[0] = descriptor_set;
    global_descriptor_sets_by_name_[global_ubo_name_id] = descriptor_set;
    global_ubo_descriptor_bound_ = false;
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

    if (material_buffer_.handle() != 0) {
        resources.release_buffer(material_buffer_.handle());
        material_buffer_.reset();
    }
    material_storage_descriptor_bound_ = false;
    material_storage_descriptor_dirty_ = false;

    {
        std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
        global_descriptor_sets_.clear();
        global_descriptor_sets_by_name_.clear();
    }
    frame_descriptor_set_layout_ = nullptr;

    // Drop the device pointer so later singleton teardown cannot recreate Vulkan objects.
    device_ = nullptr;
}

bool FrameResources::is_global_singleton_layout(const DescriptorSetLayout* layout) noexcept {
    if (layout == nullptr) {
        return false;
    }
    if (layout->has_bindless_binding()) {
        return true;
    }

    static const uint64_t kGlobalUbo = hash64("global_ubo");
    static const uint64_t kTransforms = hash64("transforms");
    static const uint64_t kGMaterials = hash64("g_materials");
    static const uint64_t kGTextures = hash64("g_textures");
    static const uint64_t kSamplers = hash64("samplers");

    const size_t bindings_count = layout->get_bindings_count();
    for (size_t i = 0; i < bindings_count; ++i) {
        const uint64_t name_id = layout->get_binding_name_id(i);
        if (name_id == kGlobalUbo ||
            name_id == kTransforms ||
            name_id == kGMaterials ||
            name_id == kGTextures ||
            name_id == kSamplers) {
            return true;
        }
    }
    return false;
}

void FrameResources::ensure_global_descriptor_sets(RHIPipelineLayout* pipeline_layout) {
    if (pipeline_layout == nullptr) {
        return;
    }

    pipeline_layout->global_descriptor_set_count_ = 0;

    for (DescriptorSetLayout* layout : pipeline_layout->descriptor_set_layouts_) {
        if (layout == nullptr || !is_global_singleton_layout(layout)) {
            continue;
        }

        const uint32_t set_index = layout->get_descriptor_set_index();
        const std::vector<uint64_t> binding_name_ids = collect_binding_name_ids(layout);
        if (binding_name_ids.empty()) {
            continue;
        }

        get_or_create_descriptor_set(
            set_index,
            binding_name_ids,
            [layout]() { return layout->allocate_descriptor_set(); });

        if (pipeline_layout->global_descriptor_set_count_ < MAX_DESCRIPTOR_SETS_PER_SHADER) {
            pipeline_layout->global_descriptor_set_indices_[pipeline_layout->global_descriptor_set_count_++] =
                set_index;
        }
    }
}

void FrameResources::bind_global_descriptor_sets(
    CommandBuffer& cmd,
    const RHIPipelineLayout* pipeline_layout) {
    if (pipeline_layout == nullptr
        || pipeline_layout->handle == 0
        || pipeline_layout->handle == InvalidUI64) {
        return;
    }

    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    for (uint8_t i = 0; i < pipeline_layout->global_descriptor_set_count_; ++i) {
        const uint32_t set_index = pipeline_layout->global_descriptor_set_indices_[i];
        if (set_index >= global_descriptor_sets_.size()) {
            continue;
        }
        DescriptorSet* descriptor_set = global_descriptor_sets_[set_index];
        if (descriptor_set == nullptr) {
            continue;
        }
        cmd.bind_descriptor_sets(
            &descriptor_set,
            set_index,
            1,
            pipeline_layout->handle);
    }
}

DescriptorSet* FrameResources::get_global_descriptor_set(uint64_t name_id) const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    auto it = global_descriptor_sets_by_name_.find(name_id);
    return it == global_descriptor_sets_by_name_.end() ? nullptr : it->second;
}

DescriptorSet* FrameResources::get_global_descriptor_set(const std::string& name) const {
    return get_global_descriptor_set(hash64(name));
}

void FrameResources::add_global_descriptor_set(uint64_t name_id, DescriptorSet* descriptor_set) {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    auto it = global_descriptor_sets_by_name_.find(name_id);
    if (it != global_descriptor_sets_by_name_.end()) {
        return;
    }
    global_descriptor_sets_by_name_.insert(std::make_pair(name_id, descriptor_set));
}

DescriptorSet* FrameResources::get_or_create_descriptor_set(
    uint32_t descriptor_set_index,
    const std::vector<uint64_t>& binding_name_ids,
    ocarina::function<DescriptorSet* ()> create_descriptor_set) {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);

    if (descriptor_set_index < global_descriptor_sets_.size() &&
        global_descriptor_sets_[descriptor_set_index] != nullptr) {
        return global_descriptor_sets_[descriptor_set_index];
    }

    if (descriptor_set_index >= global_descriptor_sets_.size()) {
        global_descriptor_sets_.resize(descriptor_set_index + 1, nullptr);
    }

    DescriptorSet* descriptor_set = create_descriptor_set();
    global_descriptor_sets_[descriptor_set_index] = descriptor_set;

    for (uint64_t name_id : binding_name_ids) {
        global_descriptor_sets_by_name_.insert(std::make_pair(name_id, descriptor_set));
    }

    return descriptor_set;
}

DescriptorSet* FrameResources::get_descriptor_set(uint32_t descriptor_set_index) const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    if (descriptor_set_index >= global_descriptor_sets_.size()) {
        return nullptr;
    }
    return global_descriptor_sets_[descriptor_set_index];
}

bool FrameResources::has_descriptor_set(uint32_t descriptor_set_index) const {
    return get_descriptor_set(descriptor_set_index) != nullptr;
}

DescriptorSet* FrameResources::find_bindless_descriptor_set_locked() const {
    auto it = global_descriptor_sets_by_name_.find(hash64("g_textures"));
    if (it != global_descriptor_sets_by_name_.end()) {
        return it->second;
    }
    it = global_descriptor_sets_by_name_.find(hash64("g_materials"));
    if (it != global_descriptor_sets_by_name_.end()) {
        return it->second;
    }
    it = global_descriptor_sets_by_name_.find(hash64("samplers"));
    return it == global_descriptor_sets_by_name_.end() ? nullptr : it->second;
}

void FrameResources::queue_bindless_texture_update(uint32_t index, Texture* texture) {
    if (texture == nullptr || index == InvalidUI32) {
        return;
    }
    pending_bindless_updates_.push(PendingBindlessUpdate{index, texture});
}

void FrameResources::flush_pending_bindless_updates() {
    DescriptorSet* bindless_set = nullptr;
    {
        std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
        bindless_set = find_bindless_descriptor_set_locked();
    }

    PendingBindlessUpdate update;
    if (bindless_set == nullptr) {
        std::vector<PendingBindlessUpdate> deferred;
        while (pending_bindless_updates_.try_pop(update)) {
            deferred.push_back(std::move(update));
        }
        for (PendingBindlessUpdate& pending : deferred) {
            pending_bindless_updates_.push(std::move(pending));
        }
        return;
    }

    while (pending_bindless_updates_.try_pop(update)) {
        if (update.texture != nullptr && update.index != InvalidUI32) {
            bindless_set->update_bindless_texture_at_index(update.index, update.texture);
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

                OC_INFO(
                    "[material_update] Texture: material=",
                    reinterpret_cast<uintptr_t>(request.material),
                    " name_id=",
                    request.name_id,
                    " tex=",
                    reinterpret_cast<uintptr_t>(texture),
                    " bindless=",
                    request.texture_handle.bindless_index_);

                DescriptorSet* descriptor_set = get_global_descriptor_set(request.name_id);
                if (descriptor_set == nullptr) {
                    descriptor_set = request.material->get_material_descriptor_set();
                }
                if (descriptor_set == nullptr) {
                    if (request.material->uses_global_material_buffer()
                        || request.material->uses_shared_bindless_descriptor_set()) {
                        OC_INFO(
                            "[material_update] Texture: bindless/global path uses_global=",
                            request.material->uses_global_material_buffer(),
                            " uses_shared_bindless=",
                            request.material->uses_shared_bindless_descriptor_set());
                        if (request.texture_handle.bindless_index_ != InvalidUI32) {
                            texture->set_gpu_resource_state(GPUResourceState::GPU_Visible);
                        }
                        return true;
                    }
                    OC_INFO("[material_update] Texture pending: descriptor set not ready");
                    return false;
                }

                descriptor_set->update_texture(request.name_id, texture);
                texture->set_gpu_resource_state(GPUResourceState::GPU_Visible);
                OC_INFO(
                    "[material_update] Texture applied: set=",
                    reinterpret_cast<uintptr_t>(descriptor_set));
                return true;
            }
            case MaterialUpdateKind::Sampler: {
                if (!request.material->is_material_infrastructure_ready()) {
                    return false;
                }

                OC_INFO(
                    "[material_update] Sampler: material=",
                    reinterpret_cast<uintptr_t>(request.material),
                    " name_id=",
                    request.name_id);
                DescriptorSet* descriptor_set = get_global_descriptor_set(request.name_id);
                if (descriptor_set == nullptr) {
                    descriptor_set = request.material->get_material_descriptor_set();
                }
                if (descriptor_set == nullptr) {
                    OC_INFO("[material_update] Sampler pending: descriptor set not ready");
                    return false;
                }

                descriptor_set->update_sampler(request.name_id, request.sampler);
                OC_INFO(
                    "[material_update] Sampler applied: set=",
                    reinterpret_cast<uintptr_t>(descriptor_set));
                return true;
            }
            case MaterialUpdateKind::UniformBuffer: {
                if (!request.material->is_material_infrastructure_ready()) {
                    return false;
                }

                if (request.material->uses_global_material_buffer()) {
                    EntityComponentSystem& ecs = EntityComponentSystem::instance();
                    if (request.material->has_material_buffer()) {
                        const uint32_t offset = request.material->material_buffer_offset();
                        const uint32_t size = request.material->material_buffer_size();
                        if (offset != InvalidUI32 && size > 0) {
                            const size_t required_bytes =
                                static_cast<size_t>(offset) + static_cast<size_t>(size);
                            if (required_bytes > material_buffer_.size_in_byte()) {
                                grow_material_gpu_buffer(required_bytes);
                            }
                            material_storage_descriptor_dirty_ = true;

                            if (material_buffer_.handle() != 0) {
                                const uint8_t* src =
                                    ecs.material_parameters_buffer().data() + offset;
                                material_buffer_.copy_from_immediately(
                                    src,
                                    size,
                                    offset);
                            }
                        }
                    }
                } else {
                    request.material->apply_material_parameters_upload();
                }
                request.material->clear_uniform_buffer_update_queued();
                return true;
            }
            default:
                return true;
        }
    });
}

bool FrameResources::is_global_descriptor_set_index(uint32_t set_index) const {
    return has_descriptor_set(set_index);
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

    if (material_buffer_.handle() == 0) {
        material_buffer_ = ResourceManager::instance().create_buffer<MaterialParams>(
            device_,
            EntityComponentSystem::kDefaultMaterialParamsCapacity,
            GraphicBufferBindFlags::StructuredBuffer,
            EntityComponentSystem::kMaterialsBufferName);
        material_storage_descriptor_bound_ = false;
        material_storage_descriptor_dirty_ = true;
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

    DescriptorSet* scene_set = get_global_descriptor_set(EntityComponentSystem::kTransformsBufferName);
    if (scene_set == nullptr) {
        return;
    }

    scene_set->update_storage_buffer(
        hash64(EntityComponentSystem::kTransformsBufferName),
        transform_buffer_.handle(),
        0,
        transform_buffer_.size_in_byte());
    transform_storage_descriptor_bound_ = true;
}

void FrameResources::upload_transform_buffer() {
    DescriptorSet* scene_set = get_global_descriptor_set(EntityComponentSystem::kTransformsBufferName);
    if (scene_set == nullptr || device_ == nullptr) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    // Avoid scanning/copying when transforms are known to be unchanged and the
    // descriptor buffer is already bound.
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

void FrameResources::grow_material_gpu_buffer(size_t byte_count) {
    if (device_ == nullptr) {
        return;
    }

    const size_t required_bytes = std::max(byte_count, size_t{1});
    const size_t required_elements = std::max(
        (required_bytes + sizeof(MaterialParams) - 1) / sizeof(MaterialParams),
        size_t{1});
    if (material_buffer_.handle() != 0 && material_buffer_.size() >= required_elements) {
        return;
    }

    const size_t grown_elements = std::max(
        required_elements,
        material_buffer_.size() == 0 ? required_elements : material_buffer_.size() * 2);

    if (material_buffer_.handle() != 0) {
        ResourceManager::instance().release_buffer(material_buffer_.handle());
        material_buffer_.reset();
    }

    material_buffer_ = ResourceManager::instance().create_buffer<MaterialParams>(
        device_,
        grown_elements,
        GraphicBufferBindFlags::StructuredBuffer,
        EntityComponentSystem::kMaterialsBufferName);
    material_storage_descriptor_bound_ = false;
    material_storage_descriptor_dirty_ = true;

    // CPU staging is the source of truth — refill the new buffer so other slots survive.
    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    const size_t used_bytes = ecs.material_parameters_buffer().size();
    if (material_buffer_.handle() != 0 && used_bytes > 0) {
        material_buffer_.copy_from_immediately(
            ecs.material_parameters_buffer().data(),
            static_cast<uint32_t>(std::min(used_bytes, material_buffer_.size_in_byte())));
    }
}

void FrameResources::bind_material_storage_buffer() {
    if (material_buffer_.handle() == 0) {
        return;
    }
    if (material_storage_descriptor_bound_) {
        material_storage_descriptor_dirty_ = false;
        return;
    }

    DescriptorSet* material_set = get_global_descriptor_set(EntityComponentSystem::kMaterialsBufferName);
    if (material_set == nullptr) {
        // Keep dirty so update_per_frame retries after the global set is registered.
        return;
    }

    material_set->update_storage_buffer(
        hash64(EntityComponentSystem::kMaterialsBufferName),
        material_buffer_.handle(),
        0,
        material_buffer_.size_in_byte());
    material_storage_descriptor_bound_ = true;
    material_storage_descriptor_dirty_ = false;
}

void FrameResources::bind_global_ubo_if_needed() {
    if (global_ubo_descriptor_bound_ || global_ubo_buffer_.handle() == 0) {
        return;
    }

    DescriptorSet* global_descriptor_set = get_global_descriptor_set("global_ubo");
    if (global_descriptor_set == nullptr) {
        return;
    }

    global_descriptor_set->update_buffer(
        hash64("global_ubo"),
        global_ubo_buffer_.handle(),
        0,
        static_cast<uint32_t>(global_ubo_buffer_.size_in_byte()));
    global_ubo_descriptor_bound_ = true;
}

void FrameResources::upload_global_uniform_buffer(Camera* camera) {
    DescriptorSet* global_descriptor_set = get_global_descriptor_set("global_ubo");
    if (global_descriptor_set == nullptr || device_ == nullptr) {
        return;
    }

    if (camera != nullptr) {
        global_ubo_.projection_matrix = camera->get_projection_matrix().transpose();
        global_ubo_.view_matrix = camera->get_view_matrix().transpose();
        const math3d::Vector3D& cam_position = camera->get_position();
        global_ubo_.camera_pos = make_float4(cam_position[0], cam_position[1], cam_position[2], 1.0f);
    }

    // Keep sun direction normalized for the shader.
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
    if (material_storage_descriptor_dirty_) {
        bind_material_storage_buffer();
    }

    // Ensure deferred constructor writes (defaults) land on the render thread
    // even for sets that were not yet bound this frame.
    {
        std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
        for (DescriptorSet* set : global_descriptor_sets_) {
            if (set != nullptr) {
                set->commit_updates();
            }
        }
    }

    if (update_) {
        update_(*this, dt);
    }
}

}// namespace ocarina
