// Copyright 2020 Google LLC
#include "material_params.hlsl"

[[vk::binding(0, 2)]] cbuffer material_ubo : register(b0)
{
    MaterialParams material;
};
[[vk::binding(1, 1)]] Texture2D g_textures[] : register(t0);
[[vk::binding(2, 1)]] SamplerState samplers[] : register(s0);

struct VSOutput
{
[[vk::location(0)]] float2 UV : TEXCOORD0;
[[vk::location(1)]] float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = g_textures[material.albedoIndex].Sample(samplers[material.albedoSamplerIndex], input.UV);
	//color *= material.baseColorFactor;
	return float4(color.rgb, 1.0);
}
