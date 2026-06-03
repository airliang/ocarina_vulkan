//
// Created by Zero on 06/06/2022.
//

#pragma once
#include "core/logging.h"
#include "gltf_async_loader.h"
#include "TaskScheduler.h"
#include "rhi/command_buffer.h"
#include "math/quaternion.h"
#include "rhi/graphics_descriptions.h"

namespace ocarina {

void GltfAsyncLoader::Execute() {
    if (!is_loaded_) {
        is_loaded_ = load_gltf_file();
    }
}

bool GltfAsyncLoader::load_gltf_file() {
    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF gltf_context;
    std::string error, warning;

    //this->device = device;

    bool binary = false;
    size_t extpos = gltf_file_.rfind('.', gltf_file_.length());
    if (extpos != std::string::npos) {
        binary = (gltf_file_.substr(extpos + 1, gltf_file_.length() - extpos) == "glb");
    }
    bool file_loaded = binary? gltf_context.LoadBinaryFromFile(&gltf_model, &error, &warning, gltf_file_)
        : gltf_context.LoadASCIIFromFile(&gltf_model, &error, &warning, gltf_file_);
    if (!file_loaded)
    {
        OC_WARNING_FORMAT("Failed to load glTF file: {}, Error: {}, Warning: {}", gltf_file_.c_str(), error.c_str(), warning.c_str());
        return false;
    }

    uint32_t total_item_count = gltf_model.scenes[0].nodes.size() + gltf_model.images.size() 
        + gltf_model.materials.size() + gltf_model.textures.size();

    for (size_t i = 0; i < gltf_model.images.size(); i++) {
        const tinygltf::Image& image = gltf_model.images[i];
    }

    const tinygltf::Scene& scene = gltf_model.scenes[0];
    for (size_t i = 0; i < scene.nodes.size(); i++) {
        const tinygltf::Node node = gltf_model.nodes[scene.nodes[i]];
        load_gltf_node(node, i, gltf_model);
    }

    return true;
}

void GltfAsyncLoader::load_gltf_node(const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model)
{
    float3 translation = float3(0.0f);
    if (node.translation.size() == 3) {
        translation = float3(node.translation[0], node.translation[1], node.translation[2]);
    }
    Quaternion rotation;
    if (node.rotation.size() == 4) {
        rotation = Quaternion(static_cast<float>(node.rotation[0]),
                              static_cast<float>(node.rotation[1]),
                              static_cast<float>(node.rotation[2]),
                              static_cast<float>(node.rotation[3]));
    }

    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const tinygltf::Primitive& primitive = mesh.primitives[i];

            Primitive* prim = new Primitive();
            prim->set_position(translation);

            // Load vertex buffer
            VertexBuffer* vb = device_->create_vertex_buffer();
            load_vertex_attributes(vb, primitive, model);
            //prim->set_vertex_buffer(vb);

            // Load index buffer
            if (primitive.indices >= 0) {
                IndexBuffer* ib = load_index_buffer(primitive, model);
                //prim->set_index_buffer(ib);
            }

            // Load material
            if (primitive.material >= 0) {
                load_material(prim, model.materials[primitive.material], model);
            }

            primitives_.push_back(prim);
        }
    }

    // Load children
    for (size_t i = 0; i < node.children.size(); ++i) {
        const tinygltf::Node& child = model.nodes[node.children[i]];
        load_gltf_node(child, node.children[i], model);
    }
}

void GltfAsyncLoader::load_vertex_attributes(VertexBuffer* vb, const tinygltf::Primitive& primitive, const tinygltf::Model& model) {
    for (auto& attr : primitive.attributes) {
        std::string name = attr.first;
        int accessorIndex = attr.second;
        const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
        const unsigned char* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
        int count = accessor.count;
        int componentType = accessor.componentType;
        int type = accessor.type;

        if (name == "POSITION") {
            if (type == TINYGLTF_TYPE_VEC3 && componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                vb->add_vertex_stream(VertexAttributeType::Enum::Position, count, sizeof(Vector3), data);
            }
        } else if (name == "NORMAL") {
            if (type == TINYGLTF_TYPE_VEC3 && componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                vb->add_vertex_stream(VertexAttributeType::Enum::Normal, count, sizeof(Vector3), data);
            }
        } else if (name == "TEXCOORD_0") {
            if (type == TINYGLTF_TYPE_VEC2 && componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                vb->add_vertex_stream(VertexAttributeType::Enum::TexCoord0, count, sizeof(Vector2), data);
            }
        }
        // Add more attributes as needed
    }
    vb->upload_data();
}

IndexBuffer* GltfAsyncLoader::load_index_buffer(const tinygltf::Primitive& primitive, const tinygltf::Model& model) {
    int accessorIndex = primitive.indices;
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const unsigned char* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    int count = accessor.count;
    int componentType = accessor.componentType;
    bool bit16 = (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    return device_->create_index_buffer((void*)data, count, bit16);
}

void GltfAsyncLoader::load_material(Primitive* prim, const tinygltf::Material& material, const tinygltf::Model& model) {
    // Load base color texture
    if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
        int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;
        const tinygltf::Texture& texture = model.textures[texIndex];
        int imageIndex = texture.source;
        const tinygltf::Image& image = model.images[imageIndex];
        // For now, assume image.uri is the path, but loading texture requires more work
        // TODO: Implement texture loading
    }
    // Add other material properties as needed
}

}