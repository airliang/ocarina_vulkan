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
    // glTF alphaCutoff (default 0.5); used by MASK / depth-prepass alpha test
    float alphaCutoff;
    // (pad keeps MaterialParams 16-byte friendly after alphaCutoff)
    float2 pad;
};
