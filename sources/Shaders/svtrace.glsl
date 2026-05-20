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

#define INFINITY        (uintBitsToFloat(0x7F800000))

vec4 Trace(SV_Tree tree, vec3 rayOrigin, vec3 rayDir)
{
    vec4 color = vec4(0.0);

    struct { uint nodePtr; vec3 nodePos; } stack[22];
    uint level = tree.depth - 1;

    const vec3 rayInvDir = 1.0 / rayDir;
    const vec3 raySign = sign(rayDir);
    const bvec3 rayDirMask = notEqual(rayDir, vec3(0));
    const vec3 rayDirBias = vec3(1e-4) * raySign;

    const vec3 sideDistStep = mix(vec3(0), rayInvDir, rayDirMask);

    vec3 cellRayOrigin;
    {
        const vec3 tMin = -rayOrigin * rayInvDir;
        const vec3 tMax = (vec3(tree.dimensions) - rayOrigin) * rayInvDir;
        const vec3 t1 = min(tMin, tMax);
        const vec3 t2 = max(tMin, tMax);
        const float tNear = max(max(t1.x, t1.y), t1.z);
        const float tFar = min(min(t2.x, t2.y), t2.z);

        if (tNear > tFar || tFar < 0.0)
            return vec4(0.0);

        cellRayOrigin = rayOrigin + 1.0001 * max(tNear, 0.0) * rayDir;

        //if (any(lessThan(cellRayOrigin, vec3(0.0))))
        //    return vec4(0.0, 1.0, 0.0, 1.0);

        //if (any(lessThan(cellRayOrigin, vec3(0))) || any(greaterThanEqual(cellRayOrigin, vec3(tree.dimensions))))
        //    return vec4(0.0, 1.0, 0.0, 1.0);
        cellRayOrigin = clamp(cellRayOrigin, vec3(0), vec3(tree.dimensions));
    }

    vec3 nodeRayOrigin = cellRayOrigin;

    vec3 nodePos = vec3(0);

    uint nodePtr = 0;
    SV_Node node = tree.pNodes.v[0];
    float scale = pow(4.0, float(tree.depth - 1));
    vec3 cellPos = scale * floor(cellRayOrigin / scale + rayDirBias);
    vec3 sideDist = ((cellPos - cellRayOrigin) + scale * (0.5 + raySign * 0.5)) * rayInvDir;
    sideDist = mix(vec3(INFINITY), sideDist, rayDirMask);

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        vec3 cellIndex = floor((cellPos - nodePos) / scale);

        if (all(greaterThanEqual(cellIndex, vec3(0))) && all(lessThan(cellIndex, vec3(4))))
        {
            uint index = uint(dot(cellIndex, vec3(1, 4, 16)));

            while (!SV_IsLeaf(node) && SV_IsOccupied(node, index))
            {
                stack[level].nodePtr = nodePtr;
                stack[level].nodePos = nodePos;
                level -= 1;

                uint childPtr = SV_GetChild(node, index);
                node = tree.pNodes.v[childPtr];
                nodePtr = childPtr;
                nodePos = cellPos;
                scale /= 4;

                cellPos = scale * floor(cellRayOrigin / scale + rayDirBias);
                sideDist = ((cellPos - cellRayOrigin) + scale * (0.5 + raySign * 0.5)) * rayInvDir;
                sideDist = mix(vec3(INFINITY), sideDist, rayDirMask);
                nodeRayOrigin = cellRayOrigin;

                cellIndex = floor((cellPos - nodePos) / scale);
                index = uint(dot(cellIndex, vec3(1, 4, 16)));
            }

            if (index >= 64)
            {
                color = vec4(1.0, 0.0, 1.0, 1.0);
                break;
            }

            if (SV_IsLeaf(node) && SV_IsOccupied(node, index))
            {   
                //color = vec4(1.0, 0.0, 0.0, 1.0);
                color = vec4((vec3(0.25) + cellIndex / 4.0), 1.0);
                break;
            }
        }
        else
        {
            if (level >= (tree.depth - 1))
                break;

            level += 1;
            uint parentPtr = stack[level].nodePtr;
            node = tree.pNodes.v[parentPtr];
            nodePos = stack[level].nodePos;
            nodePtr = parentPtr;

            scale *= 4;

            cellPos = scale * floor(cellRayOrigin / scale + rayDirBias);
            sideDist = ((cellPos - cellRayOrigin) + scale * (0.5 + raySign * 0.5)) * rayInvDir;
            sideDist = mix(vec3(INFINITY), sideDist, rayDirMask);
            nodeRayOrigin = cellRayOrigin;

            continue;    
        }

        float minSideDist = min(min(sideDist.x, sideDist.y), sideDist.z);
        cellRayOrigin = nodeRayOrigin + minSideDist * rayDir;

        vec3 incr = GetStepMask(sideDist) * raySign * scale;
        cellPos += incr;
        sideDist += incr * sideDistStep;
    }

    return color;
}