#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_atomic_float : enable

#include "common.glsl"


layout(location = 0) in vec2 v_FragPos;

layout(location = 0) out uvec4 Voxel;
layout(location = 1) out vec4 Normal;
layout(location = 2) out vec4 Radiance;



#define AMBIENT_COLOR   vec3(0.8)



vec3 SampleHemisphere(vec2 uv);
float CastSunRay(vec3 pos, vec3 normal);


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
    vec3 rayDir = cameraDir + screenPos.x * cameraPlaneU + screenPos.y * cameraPlaneV;


    // Trace primary ray:
    uvec3 voxelPos;
    uint voxelPtr;
    vec3 normal;
    vec3 hitPos;
    if (!RayTrace(pc.VoxelMap, cameraPos, rayDir, voxelPos, voxelPtr, normal, hitPos))
    {
        // No voxel has been hit.
        Normal = vec4(0.5, 0.5, 0.5, 0.0);
        Voxel = uvec4(0, 0, 0, 0xffffffff);
        Radiance = vec4(0.0);

        return;
    }

    // Sample spatiotemporal blue noise:
    ivec2 stbnUV = (ivec2(uv * pc.Resolution.xy)) & 127;
    stbnUV += ivec2(0, 128 * int(pc.FrameNo & 63));
    vec2 stbNoise = texelFetch(samplerSTBN, stbnUV, 0).rg;

    // Global illumination:
    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < 1; i++)
    {
        vec3 tangent = vec3(normal.z, normal.x, normal.y);
        vec3 bitangent = cross(normal, tangent);

        vec3 localRayDir = SampleHemisphere(stbNoise);      // ray direction in Z+ hemisphere

        vec3 rayDir = normalize(tangent * localRayDir.x + bitangent * localRayDir.y + normal * localRayDir.z);
        vec3 rayPos = (voxelPos + 0.5) + 0.500030517578125 * normal;   

        uvec3 hitVoxelPos;
        uint hitVoxelPtr;
        vec3 hitNormal;
        vec3 newHitPos;
        if (RayTrace(pc.VoxelMap, rayPos, rayDir, hitVoxelPos, hitVoxelPtr, hitNormal, newHitPos))
        {
            uint hitVoxelFace = GetVoxelFaceKey(hitVoxelPtr, hitNormal);
            VHData voxelValue = VHM_Lookup(pc.CurrentVHM, hitVoxelFace);
            float hitSunLight = clamp(voxelValue.sun / voxelValue.radiance.w, 0.0, 1.0);

            uint material = pc.VoxelMap.pVoxels.v[hitVoxelPtr];
            float emission = u_Palette[material].emission;
            
            if (emission != 0.0f)
            {
                vec3 lightColor = vec3(u_Palette[material].color) / 255.0;
                irradiance = emission * lightColor;
            }
            else
            {
                float voxelDistance = distance(vec3(voxelPos), vec3(hitVoxelPos));
                float attenuation = max(1.0 - exp(-voxelDistance / 512.0), 0.01);

                float sunAttenuation = exp(-voxelDistance / 64.0);

                irradiance = attenuation * pc.AmbientLight.rgb + sunAttenuation * pc.SunLight.rgb/* * dot(pc.SunDir.xyz, hitNormal)*/ * hitSunLight;
            } 
        }
        else
        {
            //float sunLight = abs(1.0 - dot(rayDir, vec3(0.2570805518921551, 0.0, 0.9663899781345132)));
            //irradiance = AMBIENT_COLOR + float(sunLight < 0.005) * vec3(0.9, 0.9, 0.0);

            irradiance = pc.AmbientLight.rgb;//AMBIENT_COLOR;
            break;
        }
    }


    // Cast ray towards the sun:
    float sunLight = CastSunRay(hitPos, normal);

    // Accumulate radiance per voxel face:
    uint voxelFace = GetVoxelFaceKey(voxelPtr, normal);
    VHM_AddSample(pc.CurrentVHM, voxelFace, irradiance, sunLight, pc.FrameNo);

    // Get the number of radiance samples for the current pixel
    //float numRadianceSamples = VHM_QuickLookup(pc.PrevVHM, voxelFace).w;

    // Write output attachments:
    Voxel = uvec4(voxelPos, voxelPtr);
    Normal = vec4(0.5 * (normal + 1.0), 1.0);
    Radiance = vec4(irradiance, 0.0);
}


/* Cosine-weighted hemisphere sampling */
vec3 SampleHemisphere(vec2 uv)
{
    float r = sqrt(uv.x);
    float theta = 2.0 * 3.14159265 * uv.y;

    float x = r * cos(theta);
    float y = r * sin(theta);

    float z = sqrt(1.0 - uv.x);

    return vec3(x, y, z);
}



float CastSunRay(vec3 startPos, vec3 normal)
{
    float sunIntensity = dot(pc.SunDir.xyz, normal);

    if (sunIntensity >= 0.01)
    {
        //vec3 rayPos = startPos + 0.00048828125 * normal;
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