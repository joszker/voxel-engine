#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable


#include "common.glsl"

layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;

vec3 blur(sampler2D sp, vec2 U, vec2 scale);

vec3 GetSunLight(uvec3 voxelPos, vec3 normal);

const float PHI = 1.61803398874989484820459; // Φ = Golden Ratio 

float GoldNoise(in vec2 xy, in float seed)
{
    return fract(tan(distance(xy*PHI, xy)*seed)*xy.x);
}

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
    vec3 color = mix(baseColor * ambient, baseColor * material.emission, float(material.emission != 0.0f));

    
    //if (material == 246 || material == 254 || material == 255)
    //{
    //    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    //    return;
    //}

 //   vec3 radiance = blur(samplerRadiance, v_FragPos, 1./pc.Resolution.xy);
    //texelFetch(samplerRadiance, uv, 0).rgb + 
//    FragColor = vec4(12.0 * radiance.rgb, 1.0);
//    FragColor = vec4(texelFetch(samplerRadiance, uv, 0).rgb, 1.0);
    //FragColor = vec4(voxelValue.radiance.w / 4096.0, 0, 0, 1.0);
    //vec3 count = mix(vec3(1,0,0), vec3(0,1,0), voxelValue.radiance.w / 32768.0);

    //FragColor = vec4((normal + 1.0) / 2.0, 1.0);
    //FragColor = vec4(unpackUnorm4x8(voxelFace).rgb, 1.0);

//    ivec2 stbnUV = (uv + ivec2(int(pc.FrameNo) / 64, int(pc.FrameNo) / 8192)) & 127;
//    stbnUV += ivec2(0, 128 * int(pc.FrameNo & 63));
//    FragColor = vec4(stbnUV.rg / 128.0, 0, 1);

    FragColor = vec4(color, 1.0);
//    FragColor = vec4(texelFetch(samplerRadiance, uv, 0).rgb, 1.0);

    vec2 screenPos = v_FragPos * 2.0 - 1.0;
    FragColor = mix(FragColor, 1.0 - FragColor + 0.25, step(abs(screenPos.y), 0.003) * step(abs(screenPos.x) / 0.5625, 0.025));
    FragColor = mix(FragColor, 1.0 - FragColor + 0.25, step(abs(screenPos.x) / 0.5625, 0.003) * step(abs(screenPos.y), 0.025));
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


#define pow2(x) (x * x)

const float pi = atan(1.0) * 4.0;
const int samples = 12;
const float sigma = float(samples) * 0.25;

float gaussian(vec2 i) {
    return 1.0 / (2.0 * pi * pow2(sigma)) * exp(-((pow2(i.x) + pow2(i.y)) / (2.0 * pow2(sigma))));
}

vec3 blur(sampler2D sp, vec2 uv, vec2 scale) {
    vec3 col = vec3(0.0);
    float accum = 0.0;
    float weight;
    vec2 offset;
    
    for (int x = -samples / 2; x < samples / 2; ++x) {
        for (int y = -samples / 2; y < samples / 2; ++y) {
            offset = vec2(x, y);
            weight = gaussian(offset);
            col += texture(sp, uv + scale * offset).rgb * weight;
            accum += weight;
        }
    }
    
    return col / accum;
}
