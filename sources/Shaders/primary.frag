#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable

#include "common.glsl"
#include "sobol.glsl"


layout(location = 0) in vec2 v_FragPos;

layout(location = 0) out uvec4 Voxel;
layout(location = 1) out vec4 Normal;
layout(location = 2) out vec4 Radiance;





vec3 SampleHemisphere(vec2 uv);
float CastSunRay(vec3 pos, vec3 normal, vec2 uv);


const float PI = 3.14159265359;
const float PHI = 1.61803398874989484820459; // Φ = Golden Ratio 

float GoldNoise(in vec2 xy, in float seed)
{
    return fract(tan(distance(xy*PHI, xy)*seed)*xy.x);
}


vec3 GetNoise()
{
#if 0
    float seed = uintBitsToFloat(0x3f800000 + ((pc.FrameNo % 32768u) << 8u)) - 1.0;
    vec3 noise = vec3(
        GoldNoise(v_FragPos * pc.Resolution.xy, seed + 0.1), 
        GoldNoise(v_FragPos * pc.Resolution.xy, seed + 0.2),
        GoldNoise(v_FragPos * pc.Resolution.xy, seed + 0.3)
    );
#else
    hash_seed kernel = hash_seed(initHashSeed(uvec2(v_FragPos * pc.Resolution)), uint(pc.FrameNo));
    vec3 noise = hash3(kernel, PRNG_FILTER);
#endif
    return noise;
}


void main()
{
    vec3 cameraPos = pc.CameraPos.xyz;
    vec3 cameraDir = pc.CameraDir.xyz;
    vec3 cameraRight = -normalize(cross(vec3(0, 0, 1), cameraDir));
    vec3 cameraUp = -cross(cameraDir, cameraRight);

    vec2 uv = vec2(v_FragPos.x, 1.0 - v_FragPos.y); // UV-coordinates (from 0 to 1)
    vec2 screenPos = uv * 2.0 - 1.0;                // homogeneous clip space coordinates
    vec3 cameraPlaneU = cameraRight;
    vec3 cameraPlaneV = cameraUp * (pc.Resolution.y / pc.Resolution.x);

    float cameraFov = tan(0.5 * pc.CameraFOV); // radians(90.0)
    vec3 rayDir = cameraDir + screenPos.x * cameraPlaneU * cameraFov + screenPos.y * cameraPlaneV * cameraFov;


    // Trace primary ray:
    uvec3 voxelPos;
    uint voxelPtr;
    vec3 normal;
    vec3 hitPos;
    if (!RayMarch(pc.VoxelMap, cameraPos, rayDir, voxelPos, voxelPtr, normal, hitPos))
    {
        // No voxel has been hit.
        Normal = vec4(0.5, 0.5, 0.5, 0.0);
        Voxel = uvec4(0, 0, 0, 0xffffffff);
        Radiance = vec4(0.0);

        return;
    }

    ivec2 stbnUV = ivec2(uv * pc.Resolution.xy);
    stbnUV += ivec2(int(pc.FrameNo) / 64, int(pc.FrameNo) / 8192);
    stbnUV &= 127;
    stbnUV += ivec2(0, 128 * int(pc.FrameNo & 63));

    vec3 noise = GetNoise();

    // Global illumination:
    vec4 irradiance = vec4(0.0);
    vec3 lightRadiance = vec3(0.0); 

    for (int i = 0; i < 1; i++)
    {
        vec3 tangent = vec3(normal.z, normal.x, normal.y);
        vec3 bitangent = cross(normal, tangent);

        vec3 localRayDir = SampleHemisphere(noise.xy);      // ray direction in Z+ hemisphere

        vec3 rayDir = normalize(tangent * localRayDir.x + bitangent * localRayDir.y + normal * localRayDir.z);
        vec3 rayPos = (voxelPos + 0.5) + 0.500030517578125 * normal;   
        //vec3 rayPos = hitPos + 0.500030517578125 * normal;  

        uvec3 hitVoxelPos;
        uint hitVoxelPtr;
        vec3 hitNormal;
        vec3 newHitPos;
        if (RayMarch(pc.VoxelMap, rayPos, rayDir, hitVoxelPos, hitVoxelPtr, hitNormal, newHitPos))
        {
            uint hitVoxelFace = GetVoxelFaceKey(hitVoxelPtr, hitNormal);
            VHData voxelValue = VHM_Lookup(pc.CurrentVHM, hitVoxelFace);
            float hitSunLight = clamp(voxelValue.sun / voxelValue.radiance.w, 0.0, 1.0);

            uint material = pc.VoxelMap.pVoxels.v[hitVoxelPtr];
            float emission = u_Palette[material].emission;
            
            if (emission != 0.0f)
            {
                float voxelDistance = distance(vec3(voxelPos), vec3(hitVoxelPos));
                float attenuation = exp(-voxelDistance / 64.0);
                //float attenuation = 1.0;

                vec3 lightColor = vec3(u_Palette[material].color) / 255.0;
                irradiance += vec4(attenuation * emission * lightColor, 1.0);
                lightRadiance += irradiance.rgb;
            }
            else
            {
                vec3 lightColor = vec3(u_Palette[material].color) / 255.0;

                float voxelDistance = distance(vec3(voxelPos), vec3(hitVoxelPos));
                float attenuation = max(1.0 - exp(-voxelDistance * pc.AmbientFalloff), pc.AmbientMinimum);

                float sunAttenuation = exp(-voxelDistance / 64.0);

                irradiance = vec4(attenuation * pc.AmbientLight.rgb + sunAttenuation * lightColor * pc.SunLight.rgb/* * dot(pc.SunDir.xyz, hitNormal)*/ * hitSunLight, 1.0);
            } 
        }
        else
        {
            //float sunLight = abs(1.0 - dot(rayDir, vec3(0.2570805518921551, 0.0, 0.9663899781345132)));
            //irradiance = AMBIENT_COLOR + float(sunLight < 0.005) * vec3(0.9, 0.9, 0.0);

            irradiance = vec4(pc.AmbientLight.rgb, 1.0);//AMBIENT_COLOR;
            break;
        }
    }


    // Cast ray towards the sun:
    float sunLight = CastSunRay(hitPos, normal, noise.yz);

    // Accumulate radiance per voxel face:
    uint voxelFace = GetVoxelFaceKey(voxelPtr, normal);
    VHM_AddSample(pc.CurrentVHM, voxelFace, irradiance, irradiance.w * sunLight, pc.FrameNo);

    // Write output attachments:
    Voxel = uvec4(voxelPos, voxelPtr);
    Normal = vec4(0.5 * (normal + 1.0), 1.0);
    Radiance = vec4(irradiance.rgb, 1.0);
}


/* Cosine-weighted hemisphere sampling (Z+ direction) */
vec3 SampleHemisphere(vec2 uv)
{
    float r = sqrt(uv.x);
    float theta = 2.0 * 3.14159265 * uv.y;

    float x = r * cos(theta);
    float y = r * sin(theta);

    float z = sqrt(1.0 - uv.x);

    return vec3(x, y, z);
}


/* Sample within theta cone around direction */
vec3 SampleCone(vec3 direction, float cosTheta, vec2 uv)
{
    // Uniform sampling over the cone's solid angle.
    float cosAngle = mix(1.0, cosTheta, uv.x);
    float sinAngle = sqrt(1.0 - cosAngle * cosAngle);
    float phi = 2.0 * PI * uv.y;    // azimuth [0, 2π)

    // Local direction where +Z is the cone axis.
    vec3 localDir = vec3(cos(phi) * sinAngle, sin(phi) * sinAngle, cosAngle);

    // Build an orthonormal basis around direction.
    vec3 up = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), float(abs(direction.z) < 0.999));
    vec3 tangent   = normalize(cross(up, direction));
    vec3 bitangent = cross(direction, tangent);

    return normalize(
        tangent   * localDir.x +
        bitangent * localDir.y +
        direction * localDir.z
    );
}


float CastSunRay(vec3 startPos, vec3 normal, vec2 uv)
{
    vec3 sunDir = SampleCone(pc.SunDir.xyz, pc.SunConeThetaCos, uv);

    float sunIntensity = dot(sunDir, normal);

    if (sunIntensity > 0.0) // sunIntensity >= 0.01
    {
        vec3 rayPos = startPos + 0.046875 * normal;

        uvec3 hitVoxelPos;
        uint hitVoxelPtr;
        vec3 hitNormal;
        vec3 hitPos;
        if (!RayMarch(pc.VoxelMap, rayPos, sunDir, hitVoxelPos, hitVoxelPtr, hitNormal, hitPos))
        {
            return sunIntensity;    // Ray has reached the sun
        }
    }
    return 0.0f;
}