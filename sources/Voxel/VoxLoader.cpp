#include "VoxLoader.h"
#include "SVTree.h"
#include "Base/Log.h"

#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>

#define OGT_VOX_IMPLEMENTATION
#include "3rdparty/ogt_vox.h"





struct Bounds
{
    Vec3i min;
    Vec3i max;
};

static inline size_t FlattenIndex(int x, int y, int z, int sizeX, int sizeY, int sizeZ)
{
    return size_t(x) + size_t(y) * sizeX + size_t(z) * sizeX * sizeY;
}



// Returns:
//      worldPos = rotation * localPos + translation
static Vec3i TransformPoint(
    const ogt_vox_transform& t,
    int x,
    int y,
    int z,
    int sizeX,
    int sizeY,
    int sizeZ)
{
    const float fx = float(x);
    const float fy = float(y);
    const float fz = float(z);

    const float szx = std::floor(float(sizeX) * 0.5f) + 1.0f;
    const float szy = std::floor(float(sizeY) * 0.5f) + 1.0f;
    const float szz = std::floor(float(sizeZ) * 0.5f) + 1.0f;

    const float sx = t.m00 * szx + t.m10 * szy + t.m20 * szz;
    const float sy = t.m01 * szx + t.m11 * szy + t.m21 * szz;
    const float sz = t.m02 * szx + t.m12 * szy + t.m22 * szz;

    Vec3i out;

    out.x = int(std::truncf(
        t.m00 * fx +
        t.m10 * fy +
        t.m20 * fz +
        t.m30 - sx)
    );

    out.y = int(std::truncf(
        t.m01 * fx +
        t.m11 * fy +
        t.m21 * fz +
        t.m31 - sy)
    );

    out.z = int(std::truncf(
        t.m02 * fx +
        t.m12 * fy +
        t.m22 * fz +
        t.m32 - sz)
    );

    return out;
}


// Compose transforms.
// Returns: 
//      result = parent * child
static ogt_vox_transform Compose(const ogt_vox_transform& parent, const ogt_vox_transform& child)
{
    return ogt_vox_transform_multiply(parent, child);
}

static bool IsGroupVisible(const ogt_vox_scene* scene, u32 groupIndex)
{
    while (groupIndex != k_invalid_group_index)
    {
        const ogt_vox_group& g = scene->groups[groupIndex];

        if (g.hidden)
            return false;

        if (scene->layers[g.layer_index].hidden)
            return false;

        groupIndex = g.parent_group_index;
    }

    return true;
}


static ogt_vox_transform GetWorldGroupTransform(const ogt_vox_scene* scene, u32 groupIndex)
{
    ogt_vox_transform world =
        ogt_vox_transform_get_identity();

    std::vector<u32> chain;

    while (groupIndex != k_invalid_group_index)
    {
        chain.push_back(groupIndex);
        groupIndex =
            scene->groups[groupIndex].parent_group_index;
    }

    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        world = ogt_vox_transform_multiply(
            world,
            scene->groups[*it].transform);
    }

    return world;
}

std::vector<Voxel> BuildVoxelGridFromScene(
    const ogt_vox_scene* scene,
    u32& outSizeX,
    u32& outSizeY,
    u32& outSizeZ)
{
    Bounds bounds;

    bounds.min =
    {
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max()
    };

    bounds.max =
    {
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::min()
    };

    // -------------------------------------------------------------------------
    // Pass 1: determine scene bounds
    // -------------------------------------------------------------------------
    for (u32 i = 0; i < scene->num_instances; ++i)
    {
        const ogt_vox_instance& inst = scene->instances[i];

        /*
        if (inst.hidden)
            continue;

        if (scene->layers[inst.layer_index].hidden)
            continue;

        if (!IsGroupVisible(scene, inst.group_index))
            continue;*/

        const ogt_vox_model* model =
            scene->models[inst.model_index];

        ogt_vox_transform world =
            ogt_vox_transform_multiply(
                GetWorldGroupTransform(scene, inst.group_index),
                inst.transform);

        for (u32 z = 0; z < model->size_z; ++z)
        {
            for (u32 y = 0; y < model->size_y; ++y)
            {
                for (u32 x = 0; x < model->size_x; ++x)
                {
                    size_t srcIdx =
                        x +
                        y * model->size_x +
                        z * model->size_x * model->size_y;

                    if (model->voxel_data[srcIdx] == 0)
                        continue;

                    Vec3i p = TransformPoint(
                        world,
                        int(x), int(y), int(z),
                        model->size_x, model->size_y, model->size_z
                    );

                    bounds.min.x = std::min(bounds.min.x, p.x);
                    bounds.min.y = std::min(bounds.min.y, p.y);
                    bounds.min.z = std::min(bounds.min.z, p.z);

                    bounds.max.x = std::max(bounds.max.x, p.x);
                    bounds.max.y = std::max(bounds.max.y, p.y);
                    bounds.max.z = std::max(bounds.max.z, p.z);
                }
            }
        }
    }

    outSizeX = bounds.max.x - bounds.min.x + 1;
    outSizeY = bounds.max.y - bounds.min.y + 1;
    outSizeZ = bounds.max.z - bounds.min.z + 1;

    std::vector<Voxel> grid(size_t(outSizeX) * size_t(outSizeY) * size_t(outSizeZ), Voxel{0});

    // -------------------------------------------------------------------------
    // Pass 2: write voxels
    // -------------------------------------------------------------------------
    for (u32 i = 0; i < scene->num_instances; ++i)
    {
        const ogt_vox_instance& inst = scene->instances[i];

        /*
        if (inst.hidden)
            continue;

        if (scene->layers[inst.layer_index].hidden)
            continue;

        if (!IsGroupVisible(scene, inst.group_index))
            continue;*/

        const ogt_vox_model* model =
            scene->models[inst.model_index];

        ogt_vox_transform world =
            ogt_vox_transform_multiply(
                GetWorldGroupTransform(scene, inst.group_index),
                inst.transform);

        for (u32 z = 0; z < model->size_z; ++z)
        {
            for (u32 y = 0; y < model->size_y; ++y)
            {
                for (u32 x = 0; x < model->size_x; ++x)
                {
                    size_t srcIdx = x + y * model->size_x + z * model->size_x * model->size_y;

                    uint8_t color = model->voxel_data[srcIdx];
                    if (color == 0)
                        continue;

                    Vec3i p = TransformPoint(
                        world,
                        int(x), int(y), int(z),
                        model->size_x, model->size_y, model->size_z
                    );

                    int gx = p.x - bounds.min.x;
                    int gy = p.y - bounds.min.y;
                    int gz = p.z - bounds.min.z;

                    grid[FlattenIndex(
                        gx,
                        gy,
                        gz,
                        outSizeX,
                        outSizeY,
                        outSizeZ)] = Voxel{color};
                }
            }
        }
    }

    return grid;
}



void VoxLoader::Load(const struct ogt_vox_scene *scene, struct VoxelArray *array)
{
    array->Data = BuildVoxelGridFromScene(scene, array->Size.x, array->Size.y, array->Size.z);
}