// sparse voxel ray tracing

#define ipow2(x)    (1 << (x))
#define ipow4(x)    (1 << (2 * (x)))

uint bitCount64(uint64_t x) {
    return bitCount(uint(x)) + bitCount(uint(x >> 32));
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
    uint voxelCount;
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


#define MAX_RAY_ITERATIONS  128
#define INFINITY        (uintBitsToFloat(0x7F800000))


vec3 GetStepMask(vec3 sideDist)
{
    bvec3 mask;
    bvec3 prio = lessThan(sideDist.xyz, sideDist.yzx);

    mask.x = prio.x && !prio.z;       // (T.x < T.y) && (T.x <= T.z)
    mask.y = prio.y && !prio.x;       // (T.y < T.z) && (T.y <= T.x)
    mask.z = !(mask.x || mask.y);

    return vec3(mask);
}


/*
    Voxel ray-marching procedure. For a detailed explanation see comment below.
*/
bool RayTrace(SV_Tree tree, vec3 rayOrigin, vec3 rayDir, out uvec3 oVoxelPos, out uint oVoxelPtr, out vec3 oNormal, out vec3 oHitPos)
{
    oVoxelPos = uvec3(0, 0, 0);
    oVoxelPtr = 0xffffffff;
    oNormal = vec3(0.0);

    float treeScale = 1.0 * float(ipow4(tree.depth)); /* 0.25 * */
    rayOrigin = rayOrigin / treeScale + 1.0;

    uint stack[11];
    uint level = tree.depth - 1;

    const vec3 kRayInvDir = 1.0 / rayDir;
    const vec3 kRaySign = sign(rayDir);
    const bvec3 kRayDirMask = notEqual(rayDir, vec3(0));

    const uint kMirrorMask = 
        3 * uint(rayDir.x < 0.0f) << 0 | 
        3 * uint(rayDir.y < 0.0f) << 2 | 
        3 * uint(rayDir.z < 0.0f) << 4;

    {
        const vec3 tMin = (vec3(1.0) - rayOrigin) * kRayInvDir;
        const vec3 tMax = (vec3(2.0) - rayOrigin) * kRayInvDir;
        const vec3 t1 = min(tMin, tMax);
        const vec3 t2 = max(tMin, tMax);
        const float tNear = max(max(t1.x, t1.y), t1.z);
        const float tFar = min(min(t2.x, t2.y), t2.z);

        if (tNear > tFar || tFar < 0.0)
            return false;

        rayOrigin = rayOrigin + 1.0001 * max(tNear, 0.0) * rayDir;
        rayOrigin = clamp(rayOrigin, vec3(0), vec3(1.99999988079071044921875));
    }

    // Scale = size of each cell at the current level
    uint scaleExp = 21;     // scale = 0.25
    uint nodePtr = 0;
    SV_Node node = tree.pNodes.v[nodePtr];

    vec3 pos = rayOrigin;
    pos = mix(3.0 - pos, pos, greaterThanEqual(rayDir, vec3(0.0)));
    vec3 sideDist = pos;

    for (int i = 0; i < MAX_RAY_ITERATIONS; i++)
    {
        uvec3 localCellPos = (floatBitsToUint(pos) >> uvec3(scaleExp)) & uvec3(3);
        uint childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;
        
        // Descend until we find a leaf node:
        while (!SV_IsLeaf(node) && SV_IsOccupied(node, childIndex))
        {
            stack[scaleExp / 2] = nodePtr;      // Save current node pointer.
            scaleExp -= 2;

            nodePtr = SV_GetChild(node, childIndex);
            node = tree.pNodes.v[nodePtr];

            localCellPos = (floatBitsToUint(pos) >> uvec3(scaleExp)) & uvec3(3);
            childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;            
        }

        // Check whether we found a voxel:
        if (SV_IsLeaf(node) && SV_IsOccupied(node, childIndex))
        {
            vec3 cellPos = uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << scaleExp));

            oHitPos = float(ipow4(tree.depth)) * mix(2.0 - pos, pos - 1.0, greaterThanEqual(rayDir, vec3(0.0)));
            
            // Determine the voxels' grid position:
            oVoxelPos = uvec3((cellPos - 1.0) * float(ipow4(tree.depth)));
            oVoxelPos = mix(oVoxelPos ^ (ipow4(tree.depth) - 1), oVoxelPos, greaterThanEqual(rayDir, vec3(0.0)));

            oVoxelPtr = SV_GetChild(node, childIndex);
            oNormal = -kRaySign * GetStepMask(sideDist);

            //float tNearestSide = min(min(sideDist.x, sideDist.y), sideDist.z);
            //bvec3 sideMask = greaterThanEqual(vec3(tNearestSide), sideDist);
            //oNormal = -kRaySign * vec3(sideMask);

            return true;
        }

        // Skip empty 2x2 regions at once
        uint advScaleExp = scaleExp;
        advScaleExp += uint((SV_GetChildMask(node) >> (childIndex & 0x2a) & 0x00330033) == 0);

        // Advance the ray to whichever cell it reaches first.
        float scale = uintBitsToFloat((104 + advScaleExp) << 23);
        vec3 cellPos = uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << advScaleExp));
        sideDist = ((cellPos - pos) + scale * (0.5f + abs(kRaySign) * 0.5f)) * abs(kRayInvDir);
        sideDist = mix(vec3(INFINITY), sideDist, kRayDirMask);
        
        float tNearestSide = min(min(sideDist.x, sideDist.y), sideDist.z);  // Distance to the nearest side along the ray
        pos += abs(rayDir) * tNearestSide;

        // Check whether the ray stepped out of the current node:
        uvec3 diffPos = floatBitsToUint(pos) ^ floatBitsToUint(cellPos);
        uint diffExp = findMSB((diffPos.x | diffPos.y | diffPos.z) & 0xFFAAAAAA);
        if (diffExp > scaleExp)
        {
            if (diffExp > 21)
                break;  // Stepped out of the root node.

            // Ascend back to the ancestor node:
            scaleExp = diffExp;
            nodePtr = stack[scaleExp >> 1];
            node = tree.pNodes.v[nodePtr];
        }
    }
  
    return false;
}
/*


Coordinate system:

    - Each tree is a cube in range [1.0, 2.0)
    - For all floating-point numbers in range [1.0, 2.0) only the mantissa changes, the exponent is always 0.

    - This allows us to address each cell (node or voxel) in a hierarchical manner.
    - Each 2-bit chunk addresses one of the four cells for each level.

    - E.g. at the root level the cells are addressed like this:
        - 00 00 00 00 00 00 00 00 00 00 000 == 1.00
        - 01 00 00 00 00 00 00 00 00 00 000 == 1.25
        - 10 00 00 00 00 00 00 00 00 00 000 == 1.50
        - 11 00 00 00 00 00 00 00 00 00 000 == 1.75

    - E.g. one level deep:
        - 10 00 00 00 00 00 00 00 00 00 000 == 1.5000
        - 10 01 00 00 00 00 00 00 00 00 000 == 1.5625
        - 10 10 00 00 00 00 00 00 00 00 000 == 1.6250
        - 10 11 00 00 00 00 00 00 00 00 000 == 1.6875


Finding the cell position:

    - Given a coordinate e.g. x = 1.4
    - the mantissa of 1.4 is 01100110011001100110011

    - at level 0 set the first 21 bits to zero
        - 01000000000000000000000 = 1.25

    - at level 1 set the first 19 bits to zero:
        - 01100000000000000000000 = 1.375

    ....


Index mirroring:

    - Because each node is made up of 4x4x4 cells, the cell index is an integer between 0 and 63
    where each pair of bits refers to the X, Y or Z coordinates - ZZYYXX = 111001 = (1, 2, 3)

    - The ray marcher always steps in a positive direction (using abs(rayDir) instead of rayDir),
    as it simplifies rounding the ray position to cell positions (e.g. when the ray reaches a cell boundary
    like 1.25 from a negative direction it means the cell position should actually be 1.0 and not 1.25)

    - To account for this, the corresponding bits of the cell index are flipped.
    - E.g. if the y direction is negative the cell index 010101 = (1, 1, 1), becomes 011001 = (1, 2, 1).


Ascending to ancestor node:

    - After each step we XOR the step position with the cell position.
    - Because each level uses two bits, we only look at every second mantissa bit (hence 0xFFAAAAAA) 
    of the result and find the left-most bit which is different.

    - e.g. pos and cellPos are on the same level:
        pos     = 0101
        cellPos = 0110
        0101 XOR 0110 = 0011
                         ^ ^    ==> diffExp = 19 (level 1)
            
    - e.g. pos and cellPos are not on the same level:
        pos     = 1011 (level 1)
        cellPos = 1100 (level 0)
        1011 XOR 1100 = 0111
                         ^ ^    ==> diffExp = 21 (level 0)

    - If the exponents differs we load the corresponding ancestor node 

*/