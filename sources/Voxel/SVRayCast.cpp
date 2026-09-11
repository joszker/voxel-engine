#include "SVRayCast.h"
#include "SVTree.h"





RayTraceResult RayCast(const SVTree& tree, Vec3 rayOrigin, Vec3 rayDir)
{
    const float kTreeScale = 1.0 * float(pow4(tree.GetDepth()));
    rayOrigin = rayOrigin / kTreeScale + 1.0f;

    uint stack[22];

    const Vec3 kRayInvDir = 1.0f / rayDir;
    const Vec3 kRaySign = glm::sign(rayDir);
    const glm::bvec3 kRayDirMask = glm::notEqual(rayDir, Vec3(0.0f));

    const uint kMirrorMask = 
        0b11 * (rayDir.x < 0.0f) << 0 | 
        0b11 * (rayDir.y < 0.0f) << 2 | 
        0b11 * (rayDir.z < 0.0f) << 4;

    /* Find where the ray intersects the voxel volume */
    {
        const Vec3 tMin = (Vec3(1.0f) - rayOrigin) * kRayInvDir;
        const Vec3 tMax = (Vec3(2.0f) - rayOrigin) * kRayInvDir;
        const Vec3 t1 = glm::min(tMin, tMax);
        const Vec3 t2 = glm::max(tMin, tMax);
        const float tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
        const float tFar = glm::min(glm::min(t2.x, t2.y), t2.z);

        if (tNear > tFar || tFar < 0.0)
            return {};

        // Take the intersection point as the new starting position:
        rayOrigin = rayOrigin + glm::max(tNear, 0.0f) * rayDir;
        rayOrigin = glm::clamp(rayOrigin, Vec3(0.0f), Vec3(1.99999988079071044921875f));
    }

    uint scaleExp = 21;
    uint nodePtr = 0;
    SVNode node = tree.GetRoot();

    Vec3 pos = rayOrigin;
    pos = glm::mix(3.0f - pos, pos, glm::greaterThanEqual(rayDir, Vec3(0.0f)));
    Vec3 sideDist = pos;

    for (int i = 0; i < 1024; i++)
    {
        Vec3i localCellPos = (glm::floatBitsToUint(pos) >> Vec3u(scaleExp)) & Vec3u(3);
        uint childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;

        // Descend until we find a leaf node:
        while (!node.IsLeaf && node.IsOccupied(childIndex))
        {
            stack[scaleExp >> 1] = nodePtr;           // Save current node pointer.
            scaleExp -= 2;

            nodePtr = node.GetChildPtr(childIndex);
            node = tree.GetNodes()[nodePtr];

            localCellPos = (glm::floatBitsToUint(pos) >> Vec3u(scaleExp)) & Vec3u(3);
            childIndex = (localCellPos.x + localCellPos.y * 4 + localCellPos.z * 16) ^ kMirrorMask;
        }

        // Check whether we found a voxel:
        if (node.IsLeaf && node.IsOccupied(childIndex))
        {
            Vec3 cellPos = glm::uintBitsToFloat(glm::floatBitsToUint(pos) & (0xffffffff << scaleExp));
            Vec3 hitPos = kTreeScale * glm::mix(2.0f - pos, pos - 1.0f, glm::greaterThanEqual(rayDir, Vec3(0.f)));
            
            Vec3u voxelPos = Vec3u((cellPos - 1.0f) * kTreeScale);
            voxelPos = glm::mix(voxelPos ^ (pow4(tree.GetDepth()) - 1), voxelPos, glm::greaterThanEqual(rayDir, Vec3(0.f)));

            auto stepMask = glm::lessThanEqual(sideDist, glm::min(glm::yzx(sideDist), glm::zxy(sideDist)));

            return {
                .Hit = true,
                .VoxelPtr = node.GetChildPtr(childIndex),
                .VoxelPos = voxelPos,
                .Pos = hitPos,
                .Normal = -kRaySign * Vec3(stepMask),
            };
        }

        // Find the distance to each voxel edge:
        float scale = glm::uintBitsToFloat((104 + scaleExp) << 23);
        Vec3 cellPos = glm::uintBitsToFloat(glm::floatBitsToUint(pos) & (0xffffffff << scaleExp));
        sideDist = ((cellPos - pos) + scale * (0.5f + glm::abs(kRaySign) * 0.5f)) * glm::abs(kRayInvDir);
        sideDist = glm::mix(Vec3(INFINITY), sideDist, kRayDirMask);

        // Advance to the nearest edge:
        float tNearestSide = glm::min(glm::min(sideDist.x, sideDist.y), sideDist.z);
        pos = pos + abs(rayDir) * tNearestSide;

        // Check whether the ray stepped out of the current node:
        Vec3u diffPos = glm::floatBitsToUint(pos) ^ glm::floatBitsToUint(cellPos);
        uint diffExp = 31 - std::countl_zero((diffPos.x | diffPos.y | diffPos.z) & 0xFFAAAAAA);
        if (diffExp > scaleExp)
        {
            if (diffExp > 21) break;  // Stepped out of the root node.

            // Ascend back to the ancestor node:
            scaleExp = diffExp;
            nodePtr = stack[scaleExp >> 1];
            node = tree.GetNodes()[nodePtr];
        }
    }

    return {};    
}