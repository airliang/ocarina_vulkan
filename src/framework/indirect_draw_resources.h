#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "frustum.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/resources/buffer.h"
#include <atomic>
#include <memory>

namespace ocarina {

class CommandBuffer;
class DescriptorSet;
class Device;
class Mesh;
class ShaderParameters;
class ShaderProgram;

/// Scratch buffers for one GPU-culled indirect draw.
/// Instance matrices are not stored here: they stay in the ECS transform table
/// uploaded by FrameResources as g_transforms. This object owns the visibility
/// predicates, the three-level exclusive scans, the compacted entity indices,
/// and the indexed-indirect argument buffer.
class OC_FRAMEWORK_API IndirectDrawResources : public concepts::Noncopyable {
public:
    static constexpr uint32_t kThreads = 128;
    /// 128^3 instances fit in three scan levels at the Vulkan minimum workgroup size.
    static constexpr uint32_t kMaxInstances = kThreads * kThreads * kThreads;

    IndirectDrawResources();
    ~IndirectDrawResources();

    [[nodiscard]] bool initialize(
        Device* device,
        uint32_t instance_count,
        uint32_t base_entity,
        float half_extent,
        const ocarina_string& cull_shader,
        const ocarina_string& scan_shader,
        const ocarina_string& compact_shader);

    void release();

    [[nodiscard]] bool ready() const noexcept {
        return ready_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool has_draw_args() const noexcept { return draw_args_ready_; }
    [[nodiscard]] uint32_t instance_count() const noexcept { return instance_count_; }
    [[nodiscard]] handle_ty draw_args_handle() const noexcept { return draw_args_.handle(); }
    [[nodiscard]] uint32_t draw_args_stride() const noexcept {
        return static_cast<uint32_t>(sizeof(DrawIndexedIndirectCommand));
    }

    /// Fill indexCount / firstIndex / vertexOffset once the mesh upload has finished.
    void try_set_draw_mesh(Mesh* mesh);

    /// Cull, scan, compact, then barrier the index list and the indirect args for the draw.
    void record(CommandBuffer& cmd, const Frustum& frustum, bool culling_enabled);

    void bind_visible_indices(DescriptorSet* descriptor_set);

private:
    [[nodiscard]] TypedBuffer<uint32_t> create_uint_buffer(uint32_t count, const char* name, bool indirect);
    void release_uint_buffer(TypedBuffer<uint32_t>& buffer);
    void dispatch_compute(
        CommandBuffer& cmd,
        ShaderProgram* program,
        ShaderParameters& parameters,
        const void* push_constants,
        uint32_t push_size,
        uint32_t group_count);
    void bind_scan_level(
        ShaderParameters& parameters,
        const TypedBuffer<uint32_t>& source,
        const TypedBuffer<uint32_t>& exclusive_scan,
        const TypedBuffer<uint32_t>& group_sums);
    void scan_level(
        CommandBuffer& cmd,
        ShaderParameters& parameters,
        uint32_t element_count);

    Device* device_ = nullptr;
    uint32_t instance_count_ = 0;
    uint32_t base_entity_ = 0;
    uint32_t group_count_ = 0;
    uint32_t block_count_ = 0;
    float half_extent_ = 0.5f;
    bool draw_args_ready_ = false;
    std::atomic<bool> ready_{false};

    ShaderProgram* cull_program_ = nullptr;
    ShaderProgram* scan_program_ = nullptr;
    ShaderProgram* compact_program_ = nullptr;
    std::unique_ptr<ShaderParameters> cull_params_;
    // One descriptor set per scan level. Rebinding one set inside a command buffer
    // is illegal, and the three levels read different buffers through the same shader.
    std::unique_ptr<ShaderParameters> scan_instance_params_;
    std::unique_ptr<ShaderParameters> scan_group_params_;
    std::unique_ptr<ShaderParameters> scan_block_params_;
    std::unique_ptr<ShaderParameters> compact_params_;

    TypedBuffer<uint32_t> visibility_;
    TypedBuffer<uint32_t> local_scan_;
    TypedBuffer<uint32_t> group_sums_;
    TypedBuffer<uint32_t> group_scan_;
    TypedBuffer<uint32_t> block_sums_;
    TypedBuffer<uint32_t> block_scan_;
    TypedBuffer<uint32_t> tail_sums_;
    TypedBuffer<uint32_t> visible_indices_;
    TypedBuffer<uint32_t> draw_args_;
};

}// namespace ocarina
