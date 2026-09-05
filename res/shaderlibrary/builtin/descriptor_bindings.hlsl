#pragma once

#define FRAME_SET 0
#define MATERIAL_SET 1

// FRAME_SET bindings (engine-owned singleton)
#define BIND_GLOBAL_UBO 0
#define BIND_TEXTURES 1
#define BIND_SAMPLERS 2
#define BIND_TRANSFORM 3
#define BIND_LIGHTS 4
#define BIND_PARTICLES 5

// MATERIAL_SET bindings (per-material descriptor set)
#define BIND_MATERIAL_UBO 0
