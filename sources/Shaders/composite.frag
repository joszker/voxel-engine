#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable


#include "common.glsl"

layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;


#define DEBUG 0
#define CROSSHAIR 1


void main()
{
    ivec2 uv = ivec2(v_FragPos * pc.Resolution);

    uvec4 voxel = texelFetch(samplerVoxel, uv, 0);
    uint voxelPtr = voxel.w;

    if (voxelPtr == 0xffffffff)
        discard;

    uvec3 voxelPos = voxel.xyz;
    vec3 normal = 2.0 * texelFetch(samplerNormal, uv, 0).xyz - 1.0;


    uint voxelFace = GetVoxelFaceKey(voxelPtr, normal);
    VHData voxelValue = VHM_Lookup(pc.CurrentVHM, voxelFace);
    vec3 irradiance = voxelValue.radiance.rgb / voxelValue.radiance.w;
    float sunLight = voxelValue.sun / voxelValue.radiance.w;

    vec3 diffuse = irradiance;
    diffuse += sunLight * pc.SunLight.rgb;

    uint index = pc.VoxelMap.pVoxels.v[min(voxelPtr, pc.VoxelMap.voxelCount)];
    Material material = u_Palette[index];
    vec3 baseColor = (vec3(material.color.rgb) / 255.0);

    vec3 color = mix(baseColor * diffuse, baseColor * material.emission, float(material.emission != 0.0f));

#if DEBUG
    if (pc.Debug == 1)
        color = mix(diffuse, baseColor * material.emission, float(material.emission != 0.0f));
    else if (pc.Debug == 2)
        color = texelFetch(samplerRadiance, uv, 0).rgb;
    else if (pc.Debug == 3)
        color = unpackUnorm4x8(voxelPtr).rgb;
    else if (pc.Debug == 4)
        color = vec3(voxelPos) / 1024.0;
    else if (pc.Debug == 5)
        color = texelFetch(samplerNormal, uv, 0).xyz;
#endif

    FragColor = vec4(color, 1.0);

#if CROSSHAIR
    vec2 screenPos = (v_FragPos * 2.0 - 1.0) * (pc.Resolution.x / 1280.0);
    FragColor = mix(FragColor, 1.0 - FragColor + 0.25, step(abs(screenPos.y), 0.003) * step(abs(screenPos.x) / 0.5625, 0.025));
    FragColor = mix(FragColor, 1.0 - FragColor + 0.25, step(abs(screenPos.x) / 0.5625, 0.003) * step(abs(screenPos.y), 0.025));
#endif
}