#pragma once

#define FRAME_SET 0

#if !defined(CUSTOM_DESCRIPTOR_SET)

#define SCENE_SET 1
#define MATERIAL_SET 2

#if defined(SCENE_SET)   //binding indices for scene set
#define BIND_TRANSFORM 0
#define BIND_LIGHTS 1
#define BIND_PARTICLES 2
#endif

#if defined(MATERIAL_SET)   //binding indices for material set
// Shared across all material shaders (bindless).
#define BIND_TEXTURES 0
#define BIND_SAMPLERS 1
// StructuredBuffer<MaterialParams> g_materials
#define BIND_MATERIAL 2
#endif

#endif
