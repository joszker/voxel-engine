#pragma once
#include "Base/Base.h"




struct Voxel
{
    u8 idx = 0;

    inline operator bool () { return idx != 0; }
};



struct VoxelArray
{
    std::vector<Voxel> Data;
    Vec3u Size;

    inline Voxel Get(Vec3u index) const
    {
        if (index.x >= Size.x || index.y >= Size.y || index.z >= Size.z)
            return Voxel{};

        return Data[index.z * (Size.x * Size.y) + index.y * Size.x + index.x];
    }
};


/*
    Sparse voxel tree child reference.
*/
struct SVChild
{
    size_t idx;
    enum { INVALID = SIZE_MAX };

    SVChild() : idx(INVALID) {}
    SVChild(size_t idx) : idx(idx) {}

    operator bool () const { return idx != INVALID; }
};



/*
    Sparse voxel tree node.
*/
PACK_BEGIN
struct SVNode
{
    u32 IsLeaf : 1;         // Indicates whether the node has children.
    u32 ChildrenPtr : 31;   // Relative pointer to the first child.
    u64 ChildMask;          // Children occupancy bitmask.


    // Creates a new empty node.
    inline static SVNode Empty(bool isLeaf)
    {
        return { .IsLeaf = isLeaf, .ChildrenPtr = 0, .ChildMask = 0 };
    }

    inline bool IsEmpty() const
    {
        return ChildMask == 0;
    }

    inline bool IsOccupied(uint index) const
    {
        ASSERT(index < 64);
        return (ChildMask >> index) & 1 == 1; 
    }

    inline SVChild Get(uint index) const
    {
        // Example:
        //
        // mask = 00000010000011000001000101
        //              ^     ^^     ^   ^ ^
        //              5     43     2   1 0

        if (index >= 64 || !IsOccupied(index))
        {
            return {};
        }

        return ChildrenPtr + popcnt64(ChildMask & ((1ull << index) - 1));
    }

    inline SVChild Get(Vec3u index) const
    {
        return Get(Index1DFrom3D(index));
    }


    static Vec3u Index3DFrom1D(u32 index)
    {
        ASSERT(index < 64);
        return (Vec3u(index) / Vec3u(1, 4, 16)) % Vec3u(4);
    }

    static u32 Index1DFrom3D(Vec3u index)
    {
        ASSERT(index.x < 4 && index.y < 4 && index.z < 4);
        return Dot(index, Vec3u(1, 4, 16));
    }

};
PACK_END
static_assert(sizeof(SVNode) == 12);


/*
    Sparse voxel tree.
*/
class SVTree
{
public:
    /* Creates an empty tree */
    SVTree();

    /* Creates a tree from voxel array */
    SVTree(const VoxelArray& array);


    inline Vec3u GetDimensions() const { return m_dimensions; }
    inline u32 GetDepth() const { return m_numLevels; }

    SVNode GetRoot() const;
    std::span<SVNode> GetNodes();
    std::span<Voxel> GetVoxels();

    Voxel Get(Vec3u index) const;


private:
    SVNode insertArray(const VoxelArray& array, Vec3u offset, u32 level);


    SVNode insertVoxels(std::span<Voxel> voxels, u64 voxelMask);

    SVNode insertNodes(std::span<SVNode> nodes, u64 nodeMask);


private:
    Vec3u m_dimensions;
    u32 m_numLevels;
    std::vector<SVNode> m_nodes;
    std::vector<Voxel> m_voxels;
};
