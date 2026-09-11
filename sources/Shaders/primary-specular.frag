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



#define AMBIENT_COLOR   vec3(0.8)



vec3 SampleHemisphere(vec2 uv);
float CastSunRay(vec3 pos, vec3 normal, vec2 uv);
vec4 CastDiffuseRay(vec3 noise, vec3 voxelPos, vec3 normal);
vec3 CastSpecularRay(vec3 noise, vec3 cameraPos, vec3 startPos, vec3 voxelPos, vec3 normal);


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

    float cameraFov = tan(0.5 * pc.CameraFOV);
    vec3 rayDir = cameraDir + screenPos.x * cameraPlaneU * cameraFov + screenPos.y * cameraPlaneV * cameraFov;


    // Trace primary ray:
    uvec3 voxelPos;
    uint voxelPtr;
    vec3 normal;
    vec3 incidentPos;
    if (!RayMarch(pc.VoxelMap, cameraPos, rayDir, voxelPos, voxelPtr, normal, incidentPos))
    {
        // No voxel has been hit.
        Normal = vec4(0.5, 0.5, 0.5, 0.0);
        Voxel = uvec4(0, 0, 0, 0xffffffff);
        Radiance = vec4(0.0);

        return;
    }

    uint voxelValue = pc.VoxelMap.pVoxels.v[voxelPtr];
    Material voxelMaterial = u_Palette[voxelValue];
    

    //hash_seed kernel = hash_seed(initHashSeed(uvec2(v_FragPos * pc.Resolution)), uint(pc.FrameNo));
    //vec3 noise = hash3(kernel, PRNG_FILTER);
    vec3 noise = GetNoise();

    // Global illumination:
    vec4 irradiance = vec4(0.0);
    if (true)   // voxelMaterial.specular < 1.0
    {
        irradiance = CastDiffuseRay(noise, voxelPos, normal);
    }


    vec3 specular = vec3(0.0);
    if (voxelMaterial.specular > 0.0)
    {
        specular = CastSpecularRay(noise, cameraPos, incidentPos, voxelPos, normal);
    }



    // Cast ray towards the sun:
    float sunLight = CastSunRay(incidentPos, normal, noise.yz);

    // Accumulate radiance per voxel face:
    uint voxelFace = GetVoxelFaceKey(voxelPtr, normal);
    VHM_AddSample(pc.CurrentVHM, voxelFace, irradiance, irradiance.w * sunLight, pc.FrameNo);

    // Write output attachments:
    Voxel = uvec4(voxelPos, voxelPtr);
    Normal = vec4(0.5 * (normal + 1.0), 1.0);
    Radiance = vec4(specular, 1.0);
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


vec4 CastDiffuseRay(vec3 noise, vec3 voxelPos, vec3 normal)
{
    vec4 irradiance = vec4(0.0);

    vec3 tangent = vec3(normal.z, normal.x, normal.y);
    vec3 bitangent = cross(normal, tangent);

    vec3 localRayDir = SampleHemisphere(noise.xy);      // ray direction in Z+ hemisphere

    vec3 rayDir = normalize(tangent * localRayDir.x + bitangent * localRayDir.y + normal * localRayDir.z);
    vec3 rayPos = (voxelPos + 0.5) + 0.500030517578125 * normal;  


    uvec3 hitVoxelPos;
    uint hitVoxelPtr;
    vec3 hitNormal;
    vec3 hitIncidentPos;
    if (RayMarch(pc.VoxelMap, rayPos, rayDir, hitVoxelPos, hitVoxelPtr, hitNormal, hitIncidentPos))
    {
        uint hitVoxelFace = GetVoxelFaceKey(hitVoxelPtr, hitNormal);
        VHData voxelValue = VHM_Lookup(pc.CurrentVHM, hitVoxelFace);
        float hitSunLight = clamp(voxelValue.sun / voxelValue.radiance.w, 0.0, 1.0);

        uint material = pc.VoxelMap.pVoxels.v[hitVoxelPtr];
        vec3 baseColor = (vec3(u_Palette[material].color.rgb) / 255.0);
        float emission = u_Palette[material].emission;
        
        if (emission != 0.0f)
        {
            float voxelDistance = distance(vec3(voxelPos), vec3(hitVoxelPos));
            float attenuation = exp(-voxelDistance / 64.0);
            //float attenuation = 1.0;

            vec3 lightColor = vec3(u_Palette[material].color) / 255.0;
            irradiance = vec4(attenuation * emission * lightColor, 1.0);
        }
        else
        {
            float voxelDistance = distance(vec3(voxelPos), vec3(hitVoxelPos));
            float attenuation = max(1.0 - exp(-voxelDistance / 512.0), 0.01);

            float sunAttenuation = exp(-voxelDistance / 64.0);

            irradiance = vec4(attenuation * pc.AmbientLight.rgb + sunAttenuation * pc.SunLight.rgb/* * dot(pc.SunDir.xyz, hitNormal)*/ * hitSunLight, 1.0);
        } 
    }
    else
    {
        irradiance = vec4(pc.AmbientLight.rgb, 1.0);
    }

    return irradiance;
}


vec3 CastSpecularRay(vec3 noise, vec3 cameraPos, vec3 startPos, vec3 voxelPos, vec3 normal)
{
    vec3 rayPos = startPos + 0.046875 * normal; //voxelPos + 0.5 + 0.500030517578125 * normal;
    vec3 rayDir = reflect(normalize(rayPos - cameraPos), normal);


    uvec3 hitVoxelPos;
    uint hitVoxelPtr;
    vec3 hitNormal;
    vec3 hitIncidentPos;
    if (RayMarch(pc.VoxelMap, rayPos, rayDir, hitVoxelPos, hitVoxelPtr, hitNormal, hitIncidentPos))
    {
        uint hitVoxelFace = GetVoxelFaceKey(hitVoxelPtr, hitNormal);
        uint material = pc.VoxelMap.pVoxels.v[hitVoxelPtr];
        float emission = u_Palette[material].emission;
        vec3 baseColor = vec3(u_Palette[material].color.rgb) / 255.0;

        vec4 irradiance = CastDiffuseRay(noise.zxy, hitVoxelPos, hitNormal);
        float sunLight = CastSunRay(hitIncidentPos, hitNormal, noise.yx);
        VHM_AddSample(pc.CurrentVHM, hitVoxelFace, irradiance, sunLight, pc.FrameNo);

        VHData accum = VHM_Lookup(pc.CurrentVHM, hitVoxelFace);
        vec3 diffuse = accum.radiance.rgb / accum.radiance.w;
        
        return mix(baseColor * emission * 1.25, baseColor * diffuse, float(emission == 0.0));
    }
    else
    {
        //float sunIntensity = dot(pc.SunDir.xyz, rayDir);
        float highlight = pow(max(dot(normalize(cameraPos - voxelPos), reflect(-pc.SunDir.xyz, normal)), 0.0), 512);

        return pc.SkyColor.rgb + highlight * vec3(12) * pc.SunLight.rgb;
        //return mix(vec3(0.36, 0.64, 0.78), vec3(25) * spec, float(spec > 0.95));
    }
}

/*
float CastSunRay(vec3 startPos, vec3 normal, vec2 uv)
{
    float sunIntensity = dot(pc.SunDir.xyz, normal);

    if (sunIntensity > 0.0) // sunIntensity >= 0.01
    {
        vec3 rayPos = startPos + 0.046875 * normal;

        uvec3 hitVoxelPos;
        uint hitVoxelPtr;
        vec3 hitNormal;
        vec3 hitPos;
        if (!RayTrace(pc.VoxelMap, rayPos, pc.SunDir.xyz, hitVoxelPos, hitVoxelPtr, hitNormal, hitPos))
        {
            return sunIntensity;    // Ray has reached the sun
        }
    }
    return 0.0f;
}
*/