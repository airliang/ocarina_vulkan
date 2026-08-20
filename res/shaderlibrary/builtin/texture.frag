// Copyright 2020 Google LLC

[[vk::binding(0, 1)]]Texture2D albedo : register(t1);
[[vk::binding(1, 1)]]SamplerState sampler_albedo : register(s1);

struct VSOutput
{
[[vk::location(0)]] float2 UV : TEXCOORD0;
[[vk::location(1)]] float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = albedo.Sample(sampler_albedo, input.UV);

	return float4(color.rgb, 1.0);
}