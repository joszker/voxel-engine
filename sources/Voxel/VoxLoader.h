#pragma once


class VoxLoader
{
public:
    /* Load scene parsed from vox file to into the voxel array */
    static void Load(const struct ogt_vox_scene *scene, struct VoxelArray *array);
};