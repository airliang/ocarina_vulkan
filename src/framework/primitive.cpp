//
// Created by Zero on 06/06/2022.
//

#include "primitive.h"
#include "material.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/device.h"
#include "rhi/descriptor_set.h"
#include "rhi/resources/texture_sampler.h"
#include "mesh.h"
#include "bindless_texture_registry.h"
#include "frame_resources.h"

namespace ocarina {


Primitive::~Primitive() {
    //if (vertex_buffer_) {
    //    ocarina::delete_with_allocator<VertexBuffer>(vertex_buffer_);
    //}

    //if (index_buffer_) {
    //    ocarina::delete_with_allocator<IndexBuffer>(index_buffer_);
    //}

    if (item_.push_constant_data)
    {
        ocarina::deallocate(item_.push_constant_data);
        item_.push_constant_data = nullptr;
    }
}

void Primitive::set_geometry_data_setup(Device *device, GeometryDataSetup setup) {
    geometry_data_setup_ = setup;
    if (geometry_data_setup_) {
        geometry_data_setup_(*this);
    }

    update_descriptor_sets(device);
}

//void Primitive::set_vertex_buffer(VertexBuffer *vertex_buffer) {
//    vertex_buffer_ = vertex_buffer;
//    pipeline_state_.vertex_buffer = vertex_buffer;
//}
//
//void Primitive::set_index_buffer(IndexBuffer *index_buffer) {
//    index_buffer_ = index_buffer;
//}

void Primitive::add_descriptor_set(DescriptorSet *descriptor_set) {
    auto it = std::find(descriptor_sets_.begin(), descriptor_sets_.end(), descriptor_set);
    if (it == descriptor_sets_.end()) {
        descriptor_sets_.push_back(descriptor_set);
    }
}

DrawCallItem Primitive::get_draw_call_item(Device *device, RHIRenderPass *render_pass) {
    if (update_push_constant_function_ != nullptr) {
        update_push_constant_function_(*this);
    }
    
    if (material_ == nullptr) {
        return item_;
    }

    if (descriptor_sets_dirty_ || descriptor_sets_material_ != material_) {
        update_descriptor_sets(device);
    }

    item_.vertex_buffer = mesh_->vertex_buffer();
    item_.index_buffer = mesh_->index_buffer();

    RHIPipeline* pipeline = material_->get_pipeline();
    if (pipeline == nullptr) {
        return item_;
    }

    uint32_t push_constant_size = pipeline->push_constant_size;
    if (push_constant_data_ == nullptr && push_constant_size > 0) {
        push_constant_data_ = ocarina::allocate(push_constant_size);
        memset(push_constant_data_, 0, push_constant_size);
    }

    item_.push_constant_size = pipeline->push_constant_size;
    if (item_.push_constant_size > 0) {
        if (item_.push_constant_data == nullptr) {
            item_.push_constant_data = ocarina::allocate(item_.push_constant_size);
        }
    }
    if (item_.push_constant_data != nullptr) {
        //memcpy(item_.push_constant_data, &world_matrix_, sizeof(float4x4));
        memcpy(item_.push_constant_data, push_constant_data_, item_.push_constant_size);
    }

    item_.pre_render_function = drawcall_pre_draw_function_;

    item_.pipeline = pipeline;

    return item_;
}

void Primitive::add_texture(uint64_t name_id, Texture *texture) {
    
    uint32_t bindless_index = InvalidUI32;
    for (auto &descriptor_set : descriptor_sets_) {
        descriptor_set->update_texture(name_id, texture);
    }
    TextureHandle texture_handle{bindless_index, texture};
    textures_.insert(std::make_pair(name_id, texture_handle));
    descriptor_sets_dirty_ = true;
}

Primitive::TextureHandle Primitive::get_texture_handle(uint64_t name_id) const {
    auto it = bindless_textures_.find(name_id);
    if (it != bindless_textures_.end()) {
        return it->second;
    }
    it = textures_.find(name_id);
    if (it != textures_.end()) {
        return it->second;
    }
    return TextureHandle{InvalidUI32, nullptr};
}

void Primitive::add_bindless_texture(uint64_t name_id, Texture *texture) {
    const uint32_t bindless_index = BindlessTextureRegistry::instance().allocate_index(texture);
    FrameResources::instance().update_bindless_texture_at_index(bindless_index, texture);
    bindless_textures_.insert_or_assign(name_id, TextureHandle{bindless_index, texture});
}

void Primitive::add_sampler(uint64_t name_id, const TextureSampler& sampler)
{
    samplers_.insert(std::make_pair(name_id, sampler));
    descriptor_sets_dirty_ = true;
}

void Primitive::update_descriptor_sets(Device *device) {
    if (material_ == nullptr) {
        return;
    }

    for (auto &descriptor_set : descriptor_sets_) {
        ocarina::delete_with_allocator<DescriptorSet>(descriptor_set);
    }
    descriptor_sets_.clear();
    item_.descriptor_sets.clear();
    
    const auto& descriptor_set_layouts = material_->descriptor_set_layouts();
    for (size_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
        if (FrameResources::instance().is_global_descriptor_set_index(static_cast<uint32_t>(i))) {
            continue;
        }
        if (descriptor_set_layouts[i] == nullptr) {
            continue;
        }
        DescriptorSet* descriptor_set = descriptor_set_layouts[i]->allocate_descriptor_set();
        add_descriptor_set(descriptor_set);
        if (item_.descriptor_sets.empty()) {
            item_.first_set = static_cast<uint32_t>(i);
        }
        item_.descriptor_sets.push_back(descriptor_set);
    }

    for (auto &descriptor_set : descriptor_sets_) {
        for (auto &tex_pair : textures_) {
            descriptor_set->update_texture(tex_pair.first, tex_pair.second.texture_);
        }
        for (auto& sampler_pair : samplers_) {
            descriptor_set->update_sampler(sampler_pair.first, sampler_pair.second);
        }
    }
    descriptor_sets_material_ = material_;
    descriptor_sets_dirty_ = false;
}

void Primitive::set_push_constant_variable(uint64_t name_id, const std::byte *data, size_t size) {
    if (push_constant_data_ == nullptr || material_ == nullptr) {
        return;
    }
    RHIPipeline* pipeline = material_->get_pipeline();
    if (pipeline == nullptr) {
        return;
    }
    auto it = pipeline->push_constant_variables_.find(name_id);
    if (it != pipeline->push_constant_variables_.end()) {
        memcpy(push_constant_data_ + it->second.offset, data, size);
    } 
}
}// namespace ocarina