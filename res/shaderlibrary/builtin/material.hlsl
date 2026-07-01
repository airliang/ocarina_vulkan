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
};

[[vk::binding(0, PER_OBJECT_SET)]] cbuffer material_ubo : register(b0)
{
    MaterialParams material;
};
