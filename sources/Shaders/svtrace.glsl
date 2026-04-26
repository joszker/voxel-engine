// sparse voxel ray tracing

#define ipow2(x)    (1 << (x))
#define ipow4(x)    (1 << (2 * (x)))

uint bitCount64(uint64_t x) {
    return bitCount(uint(x)) + bitCount(uint(x >> 32));
}

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

struct Ray
{
    vec3 pos;
    vec3 signDir;
    vec3 invDir;
};

Ray Ray_Init(vec3 pos, vec3 dir)
{
    return Ray(pos, sign(dir), 1.0 / dir);
}



layout(buffer_reference) readonly buffer SVT_Nodes;
layout(buffer_reference) readonly buffer SVT_Leafs;


struct SV_Node
{
    // isLeaf       : 1-bit
    // childIndex   : 31-bit
    // childMask    : 64-bit
    uint data[3];
};

struct SV_Tree
{
    ivec3 dimensions;
    uint root;
    uint depth;
    SVT_Nodes pNodes;
    SVT_Leafs pVoxels;
};


layout(std430, buffer_reference, buffer_reference_align = 4) readonly buffer SVT_Nodes
{
    SV_Node v[];
};

layout(std430, buffer_reference, buffer_reference_align = 1) readonly buffer SVT_Leafs
{
    uint8_t v[];
};


#define SV_IsLeaf(node)             bool((node).data[0] & 1)
#define SV_GetChildrenPtr(node)     ((node).data[0] >> 1)
#define SV_GetChildMask(node)       (uint64_t((node).data[1]) | uint64_t((node).data[2]) << 32)
#define SV_IsOccupied(node, index)  ((index < 64) && (((SV_GetChildMask(node) >> (index)) & 1) == 1))

uint SV_GetChild(SV_Node node, uint index)
{
    uint childrenPtr = SV_GetChildrenPtr(node);
    uint64_t childMask = SV_GetChildMask(node);

    return childrenPtr + uint(bitCount64(childMask & ((uint64_t(1) << index) - 1)));
}


uint GetVoxel(SV_Tree tree, vec3 position)
{
    int scale = int(ipow4(tree.depth - 1));
    SV_Node node = tree.pNodes.v[0];

    ivec3 pos = ivec3(position);

    if (any(lessThan(pos, ivec3(0))) || any(greaterThanEqual(pos, tree.dimensions)))
        return 0;

    while (!SV_IsLeaf(node))
    {
        ivec3 index3D = pos / scale;
        uint index = index3D.x + index3D.y * 4 + index3D.z * 16;

        if (!SV_IsOccupied(node, index))
            return 0;

        uint childPtr = SV_GetChild(node, index);
        node = tree.pNodes.v[childPtr];
        pos -= index3D * scale;
        scale /= 4;
    }


 //   uint index = dot(pos, uvec3(1, 4, 16));
    uint index = pos.x + pos.y * 4 + pos.z * 16;

    if (!SV_IsOccupied(node, index))
        return 0;

    uint voxelPtr = SV_GetChild(node, index);
    return uint(tree.pVoxels.v[voxelPtr]);
}

vec3 GetStepMask(vec3 sideDist)
{
    bvec3 mask;
    bvec3 prio = lessThan(sideDist.xyz, sideDist.yzx);

    mask.x = prio.x && !prio.z;       // (T.x < T.y) && (T.x <= T.z)
    mask.y = prio.y && !prio.x;       // (T.y < T.z) && (T.y <= T.x)
    mask.z = !(mask.x || mask.y);

    return vec3(mask);
}


#define MAX_ITERATIONS 128

vec3 Trace(SV_Tree tree, Ray ray)
{
    vec3 color = vec3(0.0);

    vec3 incr;
    vec3 mapPos = floor(ray.pos);
    vec3 sideDist = ((mapPos - ray.pos) + 0.5 + ray.signDir * 0.5) * ray.invDir;

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        incr = GetStepMask(sideDist) * ray.signDir;
        mapPos += incr;

        uint voxel = GetVoxel(tree, mapPos);
        if (voxel > 0)
        {
            color = hsv2rgb(vec3(float(voxel) / 255.0, 1.0, 1.0));
            break;
        }
        sideDist += incr * ray.invDir;
    }

    vec3 normal = -incr;

    // Ambient lighting
    vec3 ambient = vec3(0.5) * dot(abs(incr), vec3(0.5, 1.0, 0.75));

    return color * ambient;
}