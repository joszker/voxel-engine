#pragma once
#include "Base/Base.h"


class SVTree;


struct RayTraceResult
{
    bool Hit;           // whether anything has been hit.
    uintptr_t VoxelPtr; // relative voxel ptr.
    Vec3u VoxelPos;     // position within voxel grid.
    Vec3 Pos;           // position where ray intersects the voxel.
    Vec3 Normal;        // normal of intersected face.
};


/* Performs a ray cast on the sparse voxel tree */
RayTraceResult RayCast(const SVTree& tree, Vec3 rayOrigin, Vec3 rayDir);

