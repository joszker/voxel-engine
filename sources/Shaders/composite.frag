#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable


#include "common.glsl"

layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;



vec3 GetSunLight(uvec3 voxelPos, vec3 normal);


const vec3 kSunDir = vec3(0.2570805518921551, 0.0, 0.9663899781345132);

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


    vec3 ambient = irradiance;
    ambient += sunLight * pc.SunLight.rgb;//GetSunLight(voxelPos, normal);

    uint index = pc.VoxelMap.pVoxels.v[min(voxelPtr, pc.VoxelMap.voxelCount)];
    Material material = u_Palette[index];
    vec3 baseColor = (vec3(material.color.rgb) / 255.0);

    //vec3 color = 1.0 * ambient;
    vec3 color = mix(baseColor * ambient, baseColor + 0.1, float(material.emission != 0.0f));

    
    //if (material == 246 || material == 254 || material == 255)
    //{
    //    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    //    return;
    //}

    //vec3 test = texelFetch(samplerRadiance, uv, 0).rgb;

    //FragColor = vec4(voxelValue.radiance.w / 4096.0, 0, 0, 1.0);
    FragColor = vec4(color, 1.0);
}



vec3 GetSunLight(uvec3 voxelPos, vec3 normal)
{
    vec3 sunDir = vec3(0.2570805518921551, 0.0, 0.9663899781345132);

    if (abs(dot(sunDir, normal)) >= 0.01)
    {
        vec3 rayPos = (vec3(voxelPos) + 0.5) + 0.500030517578125 * normal;

        uvec3 hitVoxelPos;
        uint hitVoxelPtr;
        vec3 hitNormal;
        vec3 hitPos;
        if (!RayTrace(pc.VoxelMap, rayPos, sunDir, hitVoxelPos, hitVoxelPtr, hitNormal, hitPos))
        {
            return vec3(1.0);
        }
    }
    return vec3(0.0);
}