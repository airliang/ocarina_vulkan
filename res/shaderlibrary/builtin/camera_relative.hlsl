#pragma once

// Must stay in lockstep with Camera::kCameraRelativeRendering.
// 1: object matrices stay in absolute world space; the shader subtracts cameraPos,
// and the uploaded view matrix has its translation removed.
#ifndef SHADEROPTIONS_CAMERA_RELATIVE_RENDERING
#define SHADEROPTIONS_CAMERA_RELATIVE_RENDERING 1
#endif

float3 GetCameraPositionWS()
{
#if (SHADEROPTIONS_CAMERA_RELATIVE_RENDERING != 0)
    return float3(0.0, 0.0, 0.0);
#else
    return cameraPos.xyz;
#endif
}

float4x4 ApplyCameraTranslationToMatrix(float4x4 modelMatrix)
{
#if (SHADEROPTIONS_CAMERA_RELATIVE_RENDERING != 0)
    modelMatrix._m03_m13_m23 -= cameraPos.xyz;
#endif
    return modelMatrix;
}

float4x4 ApplyCameraTranslationToInverseMatrix(float4x4 inverseModelMatrix)
{
#if (SHADEROPTIONS_CAMERA_RELATIVE_RENDERING != 0)
    // Camera-relative position -> absolute world, then into object space.
    // HLSL constructors are row-major, so this is Translation(+cameraPos).
    float4x4 translationMatrix = float4x4(
        1.0, 0.0, 0.0, cameraPos.x,
        0.0, 1.0, 0.0, cameraPos.y,
        0.0, 0.0, 1.0, cameraPos.z,
        0.0, 0.0, 0.0, 1.0);
    return mul(inverseModelMatrix, translationMatrix);
#else
    return inverseModelMatrix;
#endif
}

float3 ApplyCameraTranslationToPosition(float3 position)
{
#if (SHADEROPTIONS_CAMERA_RELATIVE_RENDERING != 0)
    position -= cameraPos.xyz;
#endif
    return position;
}
