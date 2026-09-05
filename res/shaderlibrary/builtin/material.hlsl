#pragma once
#include "descriptor_bindings.hlsl"
#include "material_params.hlsl"

// Per-material UBO on MATERIAL_SET. Bindless textures/samplers live on FRAME_SET (frame.hlsl).
[[vk::binding(BIND_MATERIAL_UBO, MATERIAL_SET)]] cbuffer material_ubo : register(b1)
{
    MaterialParams material;
};
