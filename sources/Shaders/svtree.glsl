layout(buffer_reference) readonly buffer SVT_Nodes;
layout(buffer_reference) readonly buffer SVT_Leafs;


// Sparse voxel node
struct SV_Node
{
    // isLeaf       : 1-bit
    // childIndex   : 31-bit
    // childMask    : 64-bit
    uint data[3];
};

// Sparse voxel tree
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

uint bitCount64(uint64_t x) 
{
    return bitCount(uint(x)) + bitCount(uint(x >> 32));
}

uint SV_GetChild(SV_Node node, uint index)
{
    uint childrenPtr = SV_GetChildrenPtr(node);
    uint64_t childMask = SV_GetChildMask(node);

    return childrenPtr + uint(bitCount64(childMask & ((uint64_t(1) << index) - 1)));
}