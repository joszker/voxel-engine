#pragma once


class VoxLoader
{
public:
    static void Load(const struct ogt_vox_scene *scene, struct VoxelArray *array);
};