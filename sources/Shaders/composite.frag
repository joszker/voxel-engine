#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable


#include "common.glsl"

layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;



void main()
{
    vec2 uv = v_FragPos;

    uvec4 voxel = texture(samplerVoxel, uv);
    uint voxelPtr = voxel.w;

    if (voxelPtr == 0xffffffff)
        discard;

    uvec3 voxelPos = voxel.xyz;
    vec3 normal = 2.0 * texture(samplerNormal, uv).xyz - 1.0;


    uint voxelFace = GetVoxelFaceKey(voxelPtr, normal);
    vec3 irradiance = VHM_Lookup(pc.CurrentVHM, voxelFace).rgb;


    uint material = pc.VoxelMap.pVoxels.v[min(voxelPtr, 82530)];
    if (material == 246 || material == 254 || material == 255)
    {
        FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    FragColor = vec4(irradiance, 1.0);
}