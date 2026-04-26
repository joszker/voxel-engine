#include "SVTree.h"
#include <glm/gtx/component_wise.hpp>
#include <glm/vector_relational.hpp>


SVTree::SVTree()
{

}

SVTree::SVTree(const VoxelArray& array)
{
    // levels == smallest possible k, such that 4^k is greater or equal to the greatest dimension.
    // scale == 4^{levels}

    u32 scale = NextPowerOf2(glm::compMax(array.Size));
    scale = glm::max(scale, 4u);

    u32 levels = log2i(scale);
    levels += (levels % 2);
    scale = 1 << (levels);
    levels /= 2;

    m_nodes.resize(1);

    SVNode root = insertArray(array, Vec3u(0), levels - 1);

    m_nodes[0] = root;
    m_numLevels = levels;
    m_dimensions = array.Size;
}


SVNode SVTree::insertArray(const VoxelArray& array, Vec3u offset, u32 level)
{
    ASSERT(level < 16);

    if (level == 0)
    {
        std::array<Voxel, 64> voxels;   // child voxels
        uint voxelCount = 0;
        u64 voxelMask = 0;

        for (u32 index = 0; index < 64; index++)
        {
            Vec3u position = (Vec3u(index) / Vec3u(1, 4, 16)) % Vec3u(4);
            position += offset;

            if (Voxel voxel = array.Get(position))
            {
                voxels[voxelCount++] = voxel;
                voxelMask |= 1ull << u64(index);
            }
        }
        return insertVoxels({ voxels.data(), voxelCount }, voxelMask);
    }
    else
    {
        // scale == the size of each node, at the current level
        u32 scale = pow4(level);        // 4^{level}

        std::array<SVNode, 64> nodes;   // child nodes
        uint nodeCount = 0;
        u64 nodeMask = 0;

        for (u32 index = 0; index < 64; index++)
        {
            Vec3u position = (Vec3u(index) / Vec3u(1, 4, 16)) % Vec3u(4);

            SVNode child = insertArray(array, position * scale + offset, level - 1);

            if (!child.IsEmpty())
            {
                nodes[nodeCount++] = child;
                nodeMask |= 1ull << u64(index);
            }
        }
        return insertNodes({ nodes.data(), nodeCount }, nodeMask);
    }
}


SVNode SVTree::insertVoxels(std::span<Voxel> newVoxels, u64 voxelMask)
{
    size_t voxelPtr = m_voxels.size();
    ASSERT(voxelPtr < INT32_MAX);

    if (voxelMask != 0)
    {
        // append voxels
        m_voxels.resize(m_voxels.size() + newVoxels.size());
        memcpy(&m_voxels[0] + voxelPtr, newVoxels.data(), sizeof(Voxel) * newVoxels.size());
    }

    return SVNode { true, u32(voxelPtr), voxelMask };
}


SVNode SVTree::insertNodes(std::span<SVNode> newNodes, u64 nodeMask)
{
    size_t nodePtr = m_nodes.size();
    ASSERT(nodePtr < INT32_MAX);

    if (nodeMask != 0)
    {
        // append nodes
        m_nodes.resize(m_nodes.size() + newNodes.size());
        memcpy(&m_nodes[0] + nodePtr, newNodes.data(), sizeof(SVNode) * newNodes.size());
    }

    return SVNode { false, u32(nodePtr), nodeMask };    
}


SVNode SVTree::GetRoot() const
{
    return m_nodes[0];
}


std::span<SVNode> SVTree::GetNodes()
{
    return { m_nodes.data(), m_nodes.size() };
}


std::span<Voxel> SVTree::GetVoxels()
{
    return { m_voxels.data(), m_voxels.size() };
}


Voxel SVTree::Get(Vec3u index) const
{
    SVNode node = GetRoot();
    u32 scale = pow4(m_numLevels - 1);

    while (!node.IsLeaf)
    {
        Vec3u nodeIndex = index / scale;
        if (SVChild child = node.Get(nodeIndex))
        {
            node = m_nodes[child.idx];
            index -= nodeIndex * scale;
            scale /= 4;
        }
        else
        {
            return Voxel{};
        }
    }

    ASSERT(scale == 1);
    if (SVChild child = node.Get(index))
    {
        return m_voxels[child.idx];
    }

    return Voxel{};
}