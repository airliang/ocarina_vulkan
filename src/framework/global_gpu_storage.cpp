#include "global_gpu_storage.h"
#include "gpu_resource_thread.h"
#include "mesh.h"
#include "rhi/device.h"
#include "rhi/resources/resource.h"

namespace ocarina {

GlobalGPUStorage& GlobalGPUStorage::instance() {
    static GlobalGPUStorage s_instance;
    return s_instance;
}

GlobalGPUStorage::~GlobalGPUStorage() {
    cleanup();
}

void GlobalGPUStorage::initialize(Device* device) {
    if (device_ != nullptr && device_ != device) {
        cleanup();
    }
    device_ = device;
    allocator_.initialize(device);
}

void GlobalGPUStorage::cleanup() {
    allocator_.cleanup();
    device_ = nullptr;
}

MeshGeometrySlice GlobalGPUStorage::upload_geometry(const MeshGeometryInput& input) {
    return allocator_.upload(input);
}

void GlobalGPUStorage::upload_mesh(OwnedMeshGeometry&& geometry, Mesh* mesh) {
    auto request = std::make_shared<MeshGPUResourceRequest>();
    request->device = device_;
    request->positions = std::move(geometry.positions);
    request->normals = std::move(geometry.normals);
    request->uvs = std::move(geometry.uvs);
    request->colors = std::move(geometry.colors);
    request->indices = std::move(geometry.indices);
    request->mesh = mesh;
    if (mesh != nullptr) {
        mesh->set_geometry_slice({});
        mesh->set_gpu_resource_state(GPUResourceState::CPU_Loaded);
    }
    GPUResourceThread::instance().enqueue(std::move(request));
}

VertexBuffer* GlobalGPUStorage::vertex_buffer(uint32_t page_index) const {
    return allocator_.vertex_buffer(page_index);
}

IndexBuffer* GlobalGPUStorage::index_buffer(uint32_t page_index) const {
    return allocator_.index_buffer(page_index);
}

}// namespace ocarina
