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
    float3 pad;
};
