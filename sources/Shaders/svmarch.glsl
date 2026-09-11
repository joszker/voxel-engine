
#define INFINITY        (uintBitsToFloat(0x7F800000))
#define NEAR_P 0
#define FAR_P 1
#define MAX_RAY_ITERATIONS  384


#define ipow4(x)    (1 << (2 * (x)))
#define getCellPos(pos, scaleExp)   (uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << scaleExp)))

vec3 GetStepMask(vec3 sideDist)
{
    bvec3 mask;
    bvec3 prio = lessThan(sideDist.xyz, sideDist.yzx);

    mask.x = prio.x && !prio.z;       // (T.x < T.y) && (T.x <= T.z)
    mask.y = prio.y && !prio.x;       // (T.y < T.z) && (T.y <= T.x)
    mask.z = !(mask.x || mask.y);

    return vec3(mask);
}

vec2 VolumeTest(vec3 rayOrigin, vec3 rayInvDir)
{
    /* Based on jcgt.org/published/0007/03/04/ */

    // Efficient slab test for a ray intersecting an AABB with corners (1, 1, 1) and (2, 2, 2):
    const vec3 t0 = (vec3(1.0) - rayOrigin) * rayInvDir;
    const vec3 t1 = (vec3(2.0) - rayOrigin) * rayInvDir;
    const vec3 tMin = min(t0, t1);
    const vec3 tMax = max(t0, t1);
    const float tNear = max(max(tMin.x, tMin.y), tMin.z);
    const float tFar = min(min(tMax.x, tMax.y), tMax.z);

    return vec2(tNear, tFar); 
}



/*
    Voxel ray-marching procedure. For a detailed explanation see comment below.
*/
bool RayMarch(SV_Tree tree, vec3 rayOrigin, vec3 rayDir, out uvec3 oVoxelPos, out uint oVoxelPtr, out vec3 oNormal, out vec3 oHitPos)
{
    // Init outputs:
    oVoxelPos = uvec3(0, 0, 0);
    oVoxelPtr = 0xffffffff;
    oNormal = vec3(0.0);

    // Define constants:
    const float kTreeScale = float(ipow4(tree.depth));
    const vec3 kRayInvDir = 1.0 / rayDir;
    const vec3 kRaySign = sign(rayDir);
    const bvec3 kRayDirMask = notEqual(rayDir, vec3(0)); 
    const uint kMirrorMask = 
        3 * uint(rayDir.x < 0.0f) << 0 | 
        3 * uint(rayDir.y < 0.0f) << 2 | 
        3 * uint(rayDir.z < 0.0f) << 4;   


    // Transform ray origin into the tree's coordinate system:
    rayOrigin = rayOrigin / kTreeScale + 1.0;
    
    // Early intersection test:
    vec2 intersect = VolumeTest(rayOrigin, kRayInvDir);
    if (intersect[NEAR_P] > intersect[FAR_P] || intersect[FAR_P] < 0.0)
        return false;

    // Initial ray position:
    vec3 pos = rayOrigin + 1.0001 * max(intersect[NEAR_P], 0.0) * rayDir;
    pos = clamp(pos, vec3(0), vec3(1.99999988079071044921875));
    pos = mix(3.0 - pos, pos, greaterThanEqual(rayDir, vec3(0.0)));


    uint stack[11];
    uint scaleExp = 21;     // scale = 0.25
    uint nodePtr = 0;
    SV_Node node = tree.pNodes.v[nodePtr];

    // Start DDA
    float scale = uintBitsToFloat((104 + scaleExp) << 23);
    vec3 cellPos = uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << scaleExp));
    vec3 sideDist = ((cellPos - pos) + scale) * abs(kRayInvDir);
    vec3 incr = GetStepMask(pos) * abs(kRaySign);
    vec3 origin = pos;

    for (uint i = 0; i < MAX_RAY_ITERATIONS; i++)
    {
        uvec3 localCellPos = (floatBitsToUint(pos) >> uvec3(scaleExp)) & uvec3(3);
        uint childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;

        // Descend until we find a leaf node:
        if (!SV_IsLeaf(node) && SV_IsOccupied(node, childIndex))
        {
            do
            {
                stack[scaleExp / 2] = nodePtr;      // Save current node pointer.
                scaleExp -= 2;

                nodePtr = SV_GetChild(node, childIndex);
                node = tree.pNodes.v[nodePtr];

                localCellPos = (floatBitsToUint(pos) >> uvec3(scaleExp)) & uvec3(3);
                childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;
            }
            while (!SV_IsLeaf(node) && SV_IsOccupied(node, childIndex));

            // Restart DDA
            scale = uintBitsToFloat((104 + scaleExp) << 23);
            cellPos = uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << scaleExp));
            sideDist = ((cellPos - pos) + scale) * abs(kRayInvDir);
            origin = pos;
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
            oNormal = -kRaySign * sign(incr);

            return true;
        }

        float tNearestSide = min(min(sideDist.x, sideDist.y), sideDist.z);  // Distance to the nearest side along the ray
        pos = origin + abs(rayDir) * tNearestSide;

        incr = GetStepMask(sideDist) * abs(kRaySign);

        vec3 nextCellMin = cellPos + scale * incr;
        vec3 nextCellMax = uintBitsToFloat(floatBitsToUint(nextCellMin + scale) - 1);
        pos = clamp(pos, nextCellMin, nextCellMax);

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

            // Restart DDA
            scale = uintBitsToFloat((104 + scaleExp) << 23);
            cellPos = uintBitsToFloat(floatBitsToUint(pos) & (0xffffffff << scaleExp));
            sideDist = ((cellPos - pos) + scale) * abs(kRayInvDir);
            origin = pos;

            continue;
        }

        // DDA step:
        cellPos = nextCellMin;
        sideDist += scale * incr * abs(kRayInvDir);
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

