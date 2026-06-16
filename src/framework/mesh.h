#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "rhi/graphics_descriptions.h"

namespace ocarina {

class VertexBuffer;
class IndexBuffer;
class Device;

class Mesh {
public:
    Mesh() = default;
    virtual ~Mesh();

    OC_MAKE_MEMBER_GETTER(vertex_buffer, )
    OC_MAKE_MEMBER_GETTER(index_buffer, )
    
    static Mesh* create_quad(Device* device);
    static Mesh* create_cube(Device* device);
    static Mesh* create_sphere(Device* device);
    void set_vertex_buffer(VertexBuffer* vertex_buffer) { vertex_buffer_ = vertex_buffer; }
    void set_index_buffer(IndexBuffer* index_buffer) { index_buffer_ = index_buffer; }

    void set_local_bounds(const float3& min_point, const float3& max_point) noexcept;
    [[nodiscard]] bool has_local_bounds() const noexcept { return local_bounds_.valid; }
    [[nodiscard]] const BoundingBox& get_local_bounds() const noexcept { return local_bounds_; }

protected:
    VertexBuffer* vertex_buffer_ = nullptr;
    IndexBuffer* index_buffer_ = nullptr;
    BoundingBox local_bounds_;
};

constexpr uint32_t MAX_BUILDIN_MESH = 3;

class BuildinMesh : public concepts::Noncopyable {
public:
    enum BuildinMeshType {
        Quad = 0,
        Cube = 1,
        Sphere = 2,
        Custom = MAX_BUILDIN_MESH
    };
public:
    BuildinMesh() = default;
    ~BuildinMesh() {}
    static BuildinMesh& instance()
    {
        static BuildinMesh s_instance;
        return s_instance;
    }

    void create_buildin_mesh(Device* device);

    void cleanup();
    VertexBuffer* vertex_buffer_[MAX_BUILDIN_MESH] = { nullptr };
    IndexBuffer* index_buffer_[MAX_BUILDIN_MESH] = { nullptr };

    bool is_created() const { return is_created_; }
private:
    bool is_created_ = false;
};

class Quad : public Mesh {
public:
    Quad(Device* device);
    ~Quad();
};

class Cube : public Mesh {
public:
    explicit Cube(Device* device);
    ~Cube();
};

class Sphere : public Mesh {
public:
    explicit Sphere(Device* device, uint32_t slice_count = 32, uint32_t stack_count = 16);
    ~Sphere();
};

}// namespace ocarina