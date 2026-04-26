#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_GOOGLE_include_directive : enable


#include "svtrace.glsl"


layout(location = 0) in vec2 v_FragPos;

layout(location = 0) out vec4 FragColor;


layout(std140, binding = 0) uniform Uniform
{
    vec4 u_Resolution;
    vec4 u_Color;
    vec4 u_LightPos;
    vec4 u_LightColor;
    vec4 u_Ambient;
    
    vec4 u_CameraPos;
    vec4 u_CameraDir;       // direction (Z-basis)
    vec4 u_CameraRight;     // X-basis
    vec4 u_CameraUp;        // Y-basis

    SV_Tree u_Map;
};


void main()
{
    vec2 uv = vec2(v_FragPos.x, 1.0 - v_FragPos.y); // UV-coordinates (from 0 to 1)
    vec2 screenPos = uv * 2.0 - 1.0;                // homogeneous clip space coordinates
    vec3 cameraPlaneU = u_CameraRight.xyz;
    vec3 cameraPlaneV = u_CameraUp.xyz * (u_Resolution.y / u_Resolution.x);
    vec3 rayDir = u_CameraDir.xyz + screenPos.x * cameraPlaneU + screenPos.y * cameraPlaneV;
    vec3 rayPos = u_CameraPos.xyz;


    Ray ray = Ray_Init(rayPos, rayDir);

    FragColor = vec4(Trace(u_Map, ray), 1.0);
}