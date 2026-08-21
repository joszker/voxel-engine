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

    inline void Init(Vec3u size)
    {
        Size = size;
        Data.resize(Size.x * Size.y * Size.z);
    }

    inline Voxel Get(Vec3u index) const
    {
        if (index.x >= Size.x || index.y >= Size.y || index.z >= Size.z)
            return Voxel{};

        return Data[index.z * (Size.x * Size.y) + index.y * Size.x + index.x];
    }

    inline void Set(Vec3u index, Voxel voxel)
    {
        if (index.x >= Size.x || index.y >= Size.y || index.z >= Size.z)
            return;

        Data[index.z * (Size.x * Size.y) + index.y * Size.x + index.x] = voxel;
    }
};


/*
    Sparse voxel tree child reference.

struct SVChild
{
    size_t idx;
    enum { INVALID = SIZE_MAX };

    SVChild() : idx(INVALID) {}
    SVChild(size_t idx) : idx(idx) {}

    operator bool () const { return idx != INVALID; }
};
*/


/*
    Sparse voxel tree node.
*/
PACK_BEGIN
struct SVNode
{
    u32 IsLeaf : 1;         // Indicates whether the node has children.
    u32 ChildrenPtr : 31;   // Relative pointer to the first child.
    u64 ChildMask;          // Children occupancy bitmask.

    inline bool IsEmpty() const
    {
        return ChildMask == 0;
    }

    inline bool IsOccupied(uint index) const
    {
        ASSERT(index < 64);
        return (ChildMask >> index) & 1 == 1; 
    }


    inline uintptr_t GetChildPtr(uint index) const
    {
        // e.g.
        // ChildMask = 00000010000011000001000101
        //                   ^     ^^     ^   ^ ^
        //                   5     43     2   1 0
        ASSERT(index < 64);
        return u64(ChildrenPtr) + popcnt64(ChildMask & ((1ull << index) - 1));
    }

    inline uint GetChildrenCount()
    {
        return popcnt64(ChildMask);
    }

    inline const SVNode *Get(uint index, const SVNode *nodes) const
    {
        if (index >= 64 || !IsOccupied(index))
        {
            return nullptr;
        }

        return nodes + GetChildPtr(index);
    }
    inline const SVNode *Get(Vec3u index, const SVNode *nodes) const
    {
        return Get(Index1DFrom3D(index), nodes);
    }

    inline Voxel GetVoxel(uint index, const Voxel *voxels) const
    {
        if (index >= 64 || !IsOccupied(index))
            return {};

        return voxels[GetChildPtr(index)];
    }
    inline Voxel GetVoxel(Vec3u index, const Voxel *voxels) const
    {
        return GetVoxel(Index1DFrom3D(index), voxels);
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
    std::span<const SVNode> GetNodes() const;
    std::span<const Voxel> GetVoxels() const;

    Voxel Get(Vec3u index) const;

    void Modify(Vec3u min, Vec3u max, Voxel voxel);
    void Modify(Vec3u min, const VoxelArray& voxels);

    void Subtract(Vec3u min, const VoxelArray& mask);

    bool Dump(std::ostream& output);
    bool Load(std::istream& input);

private:
    SVNode insertArray(const VoxelArray& array, Vec3u offset, u32 level);


    SVNode insertVoxels(std::span<Voxel> voxels, u64 voxelMask);

    SVNode insertNodes(std::span<SVNode> nodes, u64 nodeMask);

    SVNode makeEmptyNode();

    void modify(uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Voxel voxel);
    SVNode create(u32 level, Vec3u min, Vec3u max, Voxel voxel);

    void modify(uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& voxels);
    SVNode create(u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& voxels);

    bool subtract(uintptr_t parentPtr, uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& voxels);

    void deleteChild(uintptr_t nodePtr, u32 childIndex);

private:
    Vec3u m_dimensions;
    u32 m_numLevels;
    std::vector<SVNode> m_nodes;
    std::vector<Voxel> m_voxels;

    friend class SVBuilder;
};
