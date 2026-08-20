#pragma once

#include "descriptor_bindings.hlsl"

[[vk::binding(0, FRAME_SET)]] cbuffer global_ubo : register(b0)
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

