#include "indirect_draw_resources.h"

#include "frame_resources.h"
#include "mesh.h"
#include "mesh_geometry.h"
#include "resource_manager.h"
#include "core/hash.h"
#include "core/logging.h"
#include "rhi/command_buffer.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_parameters.h"
#include "rhi/shader_program.h"

namespace ocarina {

namespace {

[[nodiscard]] uint32_t div_up(uint32_t count, uint32_t divisor) noexcept {
    return divisor == 0 ? 0u : (count + divisor - 1u) / divisor;
}

struct alignas(16) CullConstants {
    float4 planes[6]{};
    uint32_t instance_count = 0;
    uint32_t culling_enabled = 0;
    float half_extent = 0.5f;
    uint32_t base_entity = 0;
};

struct ScanConstants {
    uint32_t element_count = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};

struct CompactConstants {
    uint32_t instance_count = 0;
    uint32_t pad0 = 0;
    uint32_t base_entity = 0;
    uint32_t pad1 = 0;
};

static_assert(sizeof(CullConstants) == 112);
static_assert(sizeof(ScanConstants) == 16);
static_assert(sizeof(CompactConstants) == 16);

[[nodiscard]] GraphicBufferBindFlags storage_flags(bool indirect) noexcept {
    const uint32_t flags = static_cast<uint32_t>(GraphicBufferBindFlags::ShaderReadWrite)
        | (indirect ? static_cast<uint32_t>(GraphicBufferBindFlags::IndirectArgument) : 0u);
    return static_cast<GraphicBufferBindFlags>(flags);
}

void barrier_buffers(
    CommandBuffer& cmd,
    const TypedBuffer<uint32_t>* const* buffers,
    uint32_t buffer_count,
    GpuAccess src,
    GpuAccess dst) {
    BufferMemoryBarrier barriers[8]{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < buffer_count && count < 8u; ++i) {
        if (buffers[i] == nullptr || !buffers[i]->valid()) {
            continue;
        }
        barriers[count].buffer = buffers[i]->handle();
        barriers[count].offset = 0;
        barriers[count].size = buffers[i]->size_in_byte();
        barriers[count].src = src;
        barriers[count].dst = dst;
        ++count;
    }
    cmd.buffer_barriers(barriers, count);
}

}// namespace

IndirectDrawResources::IndirectDrawResources() = default;

IndirectDrawResources::~IndirectDrawResources() {
    release();
}

bool IndirectDrawResources::initialize(
    Device* device,
    uint32_t instance_count,
    uint32_t base_entity,
    float half_extent,
    const ocarina_string& cull_shader,
    const ocarina_string& scan_shader,
    const ocarina_string& compact_shader) {
    if (device == nullptr || instance_count == 0 || instance_count > kMaxInstances) {
        OC_ERROR("IndirectDrawResources::initialize requires 1..kMaxInstances instances");
        return false;
    }

    device_ = device;
    instance_count_ = instance_count;
    base_entity_ = base_entity;
    half_extent_ = half_extent;
    group_count_ = div_up(instance_count_, kThreads);
    block_count_ = div_up(group_count_, kThreads);
    if (block_count_ > kThreads) {
        OC_ERROR("IndirectDrawResources scan needs another level for this instance count");
        return false;
    }

    ResourceManager& resources = ResourceManager::instance();
    ocarina_set<ocarina_string> options;
    cull_program_ = resources.create_compute_shader_program(device, cull_shader, options);
    scan_program_ = resources.create_compute_shader_program(device, scan_shader, options);
    compact_program_ = resources.create_compute_shader_program(device, compact_shader, options);
    if (cull_program_ == nullptr || scan_program_ == nullptr || compact_program_ == nullptr) {
        OC_ERROR("IndirectDrawResources failed to compile compute shaders");
        return false;
    }

    cull_params_ = std::make_unique<ShaderParameters>(device, cull_program_);
    scan_instance_params_ = std::make_unique<ShaderParameters>(device, scan_program_);
    scan_group_params_ = std::make_unique<ShaderParameters>(device, scan_program_);
    scan_block_params_ = std::make_unique<ShaderParameters>(device, scan_program_);
    compact_params_ = std::make_unique<ShaderParameters>(device, compact_program_);

    visibility_ = create_uint_buffer(instance_count_, "indirect_visibility", false);
    local_scan_ = create_uint_buffer(instance_count_, "indirect_local_scan", false);
    group_sums_ = create_uint_buffer(group_count_, "indirect_group_sums", false);
    group_scan_ = create_uint_buffer(group_count_, "indirect_group_scan", false);
    block_sums_ = create_uint_buffer(block_count_, "indirect_block_sums", false);
    block_scan_ = create_uint_buffer(block_count_, "indirect_block_scan", false);
    tail_sums_ = create_uint_buffer(1, "indirect_tail_sums", false);
    visible_indices_ = create_uint_buffer(instance_count_, "indirect_visible_indices", false);
    draw_args_ = create_uint_buffer(
        static_cast<uint32_t>(sizeof(DrawIndexedIndirectCommand) / sizeof(uint32_t)),
        "indirect_draw_args",
        true);

    if (!visibility_.valid() || !local_scan_.valid() || !group_sums_.valid() || !group_scan_.valid()
        || !block_sums_.valid() || !block_scan_.valid() || !tail_sums_.valid()
        || !visible_indices_.valid() || !draw_args_.valid()) {
        OC_ERROR("IndirectDrawResources failed to create GPU buffers");
        release();
        return false;
    }

    cull_params_->set_buffer("visibility", visibility_.handle(), 0, visibility_.size_in_byte());
    bind_scan_level(*scan_instance_params_, visibility_, local_scan_, group_sums_);
    bind_scan_level(*scan_group_params_, group_sums_, group_scan_, block_sums_);
    bind_scan_level(*scan_block_params_, block_sums_, block_scan_, tail_sums_);
    compact_params_->set_buffer("visibility", visibility_.handle(), 0, visibility_.size_in_byte());
    compact_params_->set_buffer("localScan", local_scan_.handle(), 0, local_scan_.size_in_byte());
    compact_params_->set_buffer("groupScan", group_scan_.handle(), 0, group_scan_.size_in_byte());
    compact_params_->set_buffer("blockScan", block_scan_.handle(), 0, block_scan_.size_in_byte());
    compact_params_->set_buffer("tailSums", tail_sums_.handle(), 0, tail_sums_.size_in_byte());
    compact_params_->set_buffer("visibleIndices", visible_indices_.handle(), 0, visible_indices_.size_in_byte());
    compact_params_->set_buffer("drawArgs", draw_args_.handle(), 0, draw_args_.size_in_byte());

    ready_.store(true, std::memory_order_release);
    return true;
}

void IndirectDrawResources::release() {
    ready_.store(false, std::memory_order_release);
    cull_params_.reset();
    scan_instance_params_.reset();
    scan_group_params_.reset();
    scan_block_params_.reset();
    compact_params_.reset();
    release_uint_buffer(visibility_);
    release_uint_buffer(local_scan_);
    release_uint_buffer(group_sums_);
    release_uint_buffer(group_scan_);
    release_uint_buffer(block_sums_);
    release_uint_buffer(block_scan_);
    release_uint_buffer(tail_sums_);
    release_uint_buffer(visible_indices_);
    release_uint_buffer(draw_args_);
    draw_args_ready_ = false;
    device_ = nullptr;
}

TypedBuffer<uint32_t> IndirectDrawResources::create_uint_buffer(
    uint32_t count,
    const char* name,
    bool indirect) {
    if (device_ == nullptr || count == 0) {
        return {};
    }
    return ResourceManager::instance().create_buffer<uint32_t>(
        device_,
        count,
        storage_flags(indirect),
        ocarina_string(name));
}

void IndirectDrawResources::release_uint_buffer(TypedBuffer<uint32_t>& buffer) {
    if (buffer.handle() != 0) {
        ResourceManager::instance().release_buffer(buffer.handle());
        buffer.reset();
    }
}

void IndirectDrawResources::try_set_draw_mesh(Mesh* mesh) {
    if (draw_args_ready_ || mesh == nullptr
        || mesh->gpu_resource_state() != GPUResourceState::GPU_Ready) {
        return;
    }
    const MeshGeometrySlice& geometry = mesh->geometry_slice();
    if (!is_valid_geometry_slice(geometry)) {
        return;
    }

    DrawIndexedIndirectCommand command{};
    command.index_count = geometry.index_count;
    command.first_index = geometry.index_offset;
    command.vertex_offset = static_cast<int32_t>(geometry.vertex_offset);
    command.first_instance = 0;
    command.instance_count = 0;
    draw_args_.copy_from_immediately(&command, sizeof(command));
    draw_args_ready_ = true;
}

void IndirectDrawResources::dispatch_compute(
    CommandBuffer& cmd,
    ShaderProgram* program,
    ShaderParameters& parameters,
    const void* push_constants,
    uint32_t push_size,
    uint32_t group_count) {
    if (program == nullptr || device_ == nullptr || group_count == 0) {
        return;
    }

    program->ensure_compute_pipeline(device_);
    RHIPipeline* pipeline = program->compute_pipeline();
    if (pipeline == nullptr) {
        return;
    }

    cmd.bind_pipeline(pipeline);
    if (program->descriptor_set_layouts()[0] != nullptr) {
        FrameResources::instance().bind_global_descriptor_sets(cmd, pipeline);
    }
    parameters.bind(cmd, pipeline);
    if (push_constants != nullptr && push_size > 0) {
        cmd.push_constants(push_constants, 0, push_size, 0);
    }
    cmd.dispatch(group_count, 1, 1);
}

void IndirectDrawResources::bind_scan_level(
    ShaderParameters& parameters,
    const TypedBuffer<uint32_t>& source,
    const TypedBuffer<uint32_t>& exclusive_scan,
    const TypedBuffer<uint32_t>& group_sums) {
    parameters.set_buffer("sourceData", source.handle(), 0, source.size_in_byte());
    parameters.set_buffer("exclusiveScan", exclusive_scan.handle(), 0, exclusive_scan.size_in_byte());
    parameters.set_buffer("groupSums", group_sums.handle(), 0, group_sums.size_in_byte());
}

void IndirectDrawResources::scan_level(
    CommandBuffer& cmd,
    ShaderParameters& parameters,
    uint32_t element_count) {
    if (element_count == 0) {
        return;
    }

    ScanConstants constants{};
    constants.element_count = element_count;
    dispatch_compute(
        cmd,
        scan_program_,
        parameters,
        &constants,
        sizeof(constants),
        div_up(element_count, kThreads));
}

void IndirectDrawResources::record(
    CommandBuffer& cmd,
    const Frustum& frustum,
    bool culling_enabled) {
    if (!ready() || cull_params_ == nullptr || compact_params_ == nullptr
        || scan_instance_params_ == nullptr || scan_group_params_ == nullptr
        || scan_block_params_ == nullptr) {
        return;
    }

    CullConstants cull{};
    for (int plane_index = 0; plane_index < Frustum::plane_count; ++plane_index) {
        const FrustumPlane& plane = frustum.plane(plane_index);
        cull.planes[plane_index] = make_float4(plane.normal.x, plane.normal.y, plane.normal.z, plane.distance);
    }
    cull.instance_count = instance_count_;
    cull.culling_enabled = culling_enabled ? 1u : 0u;
    cull.half_extent = half_extent_;
    cull.base_entity = base_entity_;
    dispatch_compute(cmd, cull_program_, *cull_params_, &cull, sizeof(cull), div_up(instance_count_, kThreads));

    const TypedBuffer<uint32_t>* visibility_list[] = {&visibility_};
    barrier_buffers(cmd, visibility_list, 1, GpuAccess::ComputeWrite, GpuAccess::ComputeRead);

    scan_level(cmd, *scan_instance_params_, instance_count_);
    const TypedBuffer<uint32_t>* after_instances[] = {&local_scan_, &group_sums_};
    barrier_buffers(cmd, after_instances, 2, GpuAccess::ComputeWrite, GpuAccess::ComputeRead);

    scan_level(cmd, *scan_group_params_, group_count_);
    const TypedBuffer<uint32_t>* after_groups[] = {&group_scan_, &block_sums_};
    barrier_buffers(cmd, after_groups, 2, GpuAccess::ComputeWrite, GpuAccess::ComputeRead);

    scan_level(cmd, *scan_block_params_, block_count_);
    const TypedBuffer<uint32_t>* after_blocks[] = {&block_scan_, &tail_sums_};
    barrier_buffers(cmd, after_blocks, 2, GpuAccess::ComputeWrite, GpuAccess::ComputeRead);

    CompactConstants compact{};
    compact.instance_count = instance_count_;
    compact.base_entity = base_entity_;
    dispatch_compute(
        cmd,
        compact_program_,
        *compact_params_,
        &compact,
        sizeof(compact),
        div_up(instance_count_, kThreads));

    BufferMemoryBarrier draw_barriers[2]{};
    draw_barriers[0].buffer = visible_indices_.handle();
    draw_barriers[0].size = visible_indices_.size_in_byte();
    draw_barriers[0].src = GpuAccess::ComputeWrite;
    draw_barriers[0].dst = GpuAccess::VertexRead;
    draw_barriers[1].buffer = draw_args_.handle();
    draw_barriers[1].size = draw_args_.size_in_byte();
    draw_barriers[1].src = GpuAccess::ComputeWrite;
    draw_barriers[1].dst = GpuAccess::IndirectRead;
    cmd.buffer_barriers(draw_barriers, 2);
}

void IndirectDrawResources::bind_visible_indices(DescriptorSet* descriptor_set) {
    if (descriptor_set == nullptr || !visible_indices_.valid()) {
        return;
    }
    descriptor_set->update_storage_buffer(
        hash64("visibleIndices"),
        visible_indices_.handle(),
        0,
        visible_indices_.size_in_byte());
}

}// namespace ocarina
