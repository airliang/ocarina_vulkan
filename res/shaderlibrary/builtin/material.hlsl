#pragma once
#include "descriptor_bindings.hlsl"
#include "material_params.hlsl"

// Shared bindless tables first so every material shader can reuse the same MATERIAL_SET
// layout. g_materials is a structured table indexed by pushConstants.material_index.
[[vk::binding(BIND_TEXTURES, MATERIAL_SET)]] Texture2D g_textures[] : register(t0);
[[vk::binding(BIND_SAMPLERS, MATERIAL_SET)]] SamplerState samplers[] : register(s0);
[[vk::binding(BIND_MATERIAL, MATERIAL_SET)]] StructuredBuffer<MaterialParams> g_materials : register(t1);

MaterialParams LoadMaterial(uint material_index)
{
    return g_materials[material_index];
}
