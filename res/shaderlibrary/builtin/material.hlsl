#pragma once

struct MaterialParams
{
    float4 baseColorFactor;
    float roughness;
    float metallic;
    float ao;
    uint albedoIndex;
    uint normalIndex;
    uint albedoSamplerIndex;
    uint normalSamplerIndex;
    // 0xffffffff = no map; sample G=roughness, B=metallic (glTF), R often packed AO
    uint metallicRoughnessIndex;
    uint metallicRoughnessSamplerIndex;
};

[[vk::binding(0, PER_OBJECT_SET)]] cbuffer material_ubo : register(b0)
{
    MaterialParams material;
};

[[vk::binding(1, MATERIAL_SET)]]Texture2D g_textures[];
[[vk::binding(2, MATERIAL_SET)]]SamplerState samplers[];
