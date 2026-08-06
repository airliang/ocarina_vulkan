// Copyright 2020 Google LLC
#include "common.hlsl"
#include "material.hlsl"

struct VSOutput
{
[[vk::location(0)]] float2 UV : TEXCOORD0;
[[vk::location(1)]] float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = g_textures[material.albedoIndex].Sample(samplers[material.albedoSamplerIndex], input.UV);

	return float4(color.rgb, 1.0);
}
