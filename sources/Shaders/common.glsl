


#include "svtree.glsl"
#include "svmarch.glsl"
//#include "svtest/svtraceA.glsl"
#include "hashmap.glsl"


struct Material
{
    u8vec4 color;
    float emission;
};



layout(push_constant, std430) uniform PushConstants
{
    vec4 AmbientLight;
    vec3 SunDir;
    float SunConeThetaCos;
    vec4 SunLight;

    vec4 CameraPos;
    vec4 CameraDir;
    vec2 Resolution;
    uint FrameNo;
    uint CurrentGen;

    SV_Tree VoxelMap;
    VoxelHashMap CurrentVHM;
    VoxelHashMap BackupVHM;    

} pc;

layout(binding = 0) uniform usampler2D samplerVoxel;
layout(binding = 1) uniform sampler2D samplerNormal;
layout(binding = 2) uniform sampler2D samplerSTBN;
layout(binding = 3) uniform sampler2D samplerRadiance;
layout(binding = 4) uniform sampler2D samplerSpecularSun;


layout (binding = 5) uniform Palette
{
    Material u_Palette[256];
};




uint GetVoxelFaceKey(uint voxelPtr, vec3 normal)
{
    uint faceIndex = int(dot(normal, vec3(1, 2, 3)) + 0.1) + 2;
    return voxelPtr * 6 + faceIndex;
}