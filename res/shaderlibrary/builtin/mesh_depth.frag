// Depth-only prepass with albedo alpha test (glTF MASK / cutout).
#include "frame.hlsl"
#include "material.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
[[vk::location(2)]] float2 UV : TEXCOORD0;
};

void main(VSOutput input)
{
    float alpha = material.baseColorFactor.a;
    if (material.albedoIndex != 0xffffffffu)
    {
        float4 sampled = g_textures[material.albedoIndex].Sample(
            g_samplers[material.albedoSamplerIndex], input.UV);
        alpha *= sampled.a;
    }
    clip(alpha - material.alphaCutoff);
}
