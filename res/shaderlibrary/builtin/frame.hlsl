#pragma once

#include "descriptor_bindings.hlsl"

[[vk::binding(BIND_GLOBAL_UBO, FRAME_SET)]] cbuffer global_ubo : register(b0)
{ 
	float4x4 projectionMatrix;
	float4x4 viewMatrix;
	float4 cameraPos;
	float4 lightPos;
	float4 sunDirection;   // xyz = world-space direction the sun light travels
	float4 sunColor;       // rgb = sun tint
	float sunIntensity;
	float3 sunPad;
};

[[vk::binding(BIND_TEXTURES, FRAME_SET)]] Texture2D g_textures[] : register(t0);
[[vk::binding(BIND_SAMPLERS, FRAME_SET)]] SamplerState g_samplers[] : register(s0);

struct Transform
{
    float4x4 modelMatrix;
    float4x4 modelMatrixInverse;
};

[[vk::binding(BIND_TRANSFORM, FRAME_SET)]] StructuredBuffer<Transform> g_transforms : register(t1);

Transform LoadTransform(uint transformIndex)
{
    return g_transforms[transformIndex];
}
