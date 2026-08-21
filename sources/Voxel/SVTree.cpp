#include "SVTree.h"
#include "Base/IO.h"
#include "Base/Log.h"

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
        // scale == the size of each node at the current level
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

std::span<const SVNode> SVTree::GetNodes() const
{
    return { m_nodes.data(), m_nodes.size() };
}

std::span<const Voxel> SVTree::GetVoxels() const
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
        if (auto *child = node.Get(nodeIndex, m_nodes.data()))
        {
            node = *child;
            index -= nodeIndex * scale;
            scale /= 4;
        }
        else
        {
            return Voxel{};
        }
    }

    ASSERT(scale == 1);
    return node.GetVoxel(index, m_voxels.data());
}


void SVTree::Modify(Vec3u min, Vec3u max, Voxel voxel)
{
    if (glm::any(glm::greaterThanEqual(min, max)))
        return;

    ASSERT(glm::all(glm::greaterThanEqual(min, Vec3u(0))));
    ASSERT(glm::all(glm::lessThanEqual(max, Vec3u(pow4(m_numLevels)))));

    modify(0, m_numLevels - 1, min, max, voxel);
}


void SVTree::Modify(Vec3u min, const VoxelArray& voxels)
{
    Vec3u max = min + voxels.Size;
 
    ASSERT(glm::all(glm::greaterThanEqual(min, Vec3u(0))));
    ASSERT(glm::all(glm::lessThanEqual(max, Vec3u(pow4(m_numLevels)))));

    modify(0, m_numLevels - 1, min, max, Vec3u(0), voxels);
}


void SVTree::modify(uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Voxel voxel)
{
    u32 scale = pow4(level);

    SVNode node = m_nodes[nodePtr];
    if (!node.IsLeaf)
    {
        Vec3u localMin = glm::min(min / scale, Vec3u(3));
        Vec3u localMax = glm::min((max - 1u) / scale, Vec3u(3)) + 1u;

        u64 modifyMask = 0;
        for (u32 z = localMin.z; z < localMax.z; z++)
            for (u32 y = localMin.y; y < localMax.y; y++)
                for (u32 x = localMin.x; x < localMax.x; x++)
                    modifyMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));


        if ((node.ChildMask & modifyMask) == modifyMask)
        {
            /* Every node that has to be modified already exists*/

            for (u32 z = localMin.z; z < localMax.z; z++)
            {
                for (u32 y = localMin.y; y < localMax.y; y++)
                {
                    for (u32 x = localMin.x; x < localMax.x; x++)
                    {
                        u32 index = SVNode::Index1DFrom3D({ x, y, z });

                        Vec3u newMin = glm::max(min, Vec3u(x, y, z) * scale) & (scale - 1);
                        Vec3u newMax = ((glm::min(max, (Vec3u(x, y, z) + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                        if ((modifyMask >> index) & 1)
                        {
                            modify(node.GetChildPtr(index), level - 1, newMin, newMax, voxel);
                        }
                    }
                }
            }

            /*
            for (u32 index = 0; index < 64; index++)
            {
                Vec3u pos = (index >> Vec3u(0, 2, 4)) & 3u;
                Vec3u newMin = min & (scale - 1);
                Vec3u newMax = (glm::min(max - 1u, pos * 4u + 3u) & (scale - 1)) + 1u;

                if ((modifyMask >> index) & 1)
                {
                    modify(node.GetChildPtr(index), level - 1, newMin, newMax, voxel);
                }
            }*/
        }
        else
        {
            /* Some of the nodes have to be created */

            std::array<SVNode, 64> children{};
            u32 srcIndex = 0;
            u32 dstIndex = 0;

            for (u32 bit = 0; bit < 64; bit++)
            {
                Vec3u pos = (bit >> Vec3u(0, 2, 4)) & 3u;

                Vec3u newMin = glm::max(min, pos * scale) & (scale - 1);
                Vec3u newMax = ((glm::min(max, (pos + 1u) * scale) - 1u) & (scale - 1)) + 1u;
                
                if (node.IsOccupied(bit))
                {
                    uintptr_t childPtr = node.ChildrenPtr + srcIndex++;
                    if ((modifyMask >> bit) & 1)
                    {
                        modify(childPtr, level - 1, newMin, newMax, voxel);
                    }
                    children[dstIndex++] = m_nodes[childPtr];
                }
                else if ((modifyMask >> bit) & 1)
                {
                    children[dstIndex++] = create(level - 1, newMin, newMax, voxel);
                }
            }

            m_nodes[nodePtr] = insertNodes({ children.data(), dstIndex }, node.ChildMask | modifyMask);
        }
    }
    else
    {
        u64 modifyMask = 0;
        for (u32 z = min.z; z < max.z; z++)
            for (u32 y = min.y; y < max.y; y++)
                for (u32 x = min.x; x < max.x; x++)
                    modifyMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));


        if (node.ChildMask == modifyMask)
        {
            memset(m_voxels.data() + node.ChildrenPtr, voxel.idx, sizeof(voxel) * node.GetChildrenCount());
        }
        else
        {
            std::array<Voxel, 64> voxels{};
            u32 dstIndex = 0;

            for (u32 bit = 0; bit < 64; bit++)
            {
                if ((modifyMask >> bit) & 1)
                {
                    voxels[dstIndex++] = voxel;
                }
                else if ((node.ChildMask >> bit) & 1)
                {
                    voxels[dstIndex++] = m_voxels[node.GetChildPtr(bit)]; 
                }
            }

            m_nodes[nodePtr] = insertVoxels({ voxels.data(), dstIndex }, node.ChildMask | modifyMask);
        }
    }
}

// NOTE: always returns non-empty node
SVNode SVTree::create(u32 level, Vec3u min, Vec3u max, Voxel voxel)
{
    u32 scale = pow4(level);

    if (level > 0)
    {
        u64 nodeMask = 0;
        uint nodeCount = 0;
        std::array<SVNode, 64> nodes{};

        Vec3u localMin = glm::min(min / scale, Vec3u(3));
        Vec3u localMax = glm::min((max - 1u) / scale, Vec3u(3)) + 1u;

        for (u32 z = localMin.z; z < localMax.z; z++)
        {
            for (u32 y = localMin.y; y < localMax.y; y++)
            {
                for (u32 x = localMin.x; x < localMax.x; x++)
                {
                    Vec3u newMin = glm::max(min, Vec3u(x, y, z) * scale) & (scale - 1);
                    Vec3u newMax = ((glm::min(max, (Vec3u(x, y, z) + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                    nodes[nodeCount++] = create(level - 1, newMin, newMax, voxel);
                    nodeMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));
                }
            }
        }
        ASSERT(nodeMask != 0);
        return insertNodes({ nodes.data(), nodeCount }, nodeMask);
    }
    else
    {
        u64 voxelMask = 0;
        uint voxelCount = 0;
        std::array<Voxel, 64> voxels{};

        for (u32 z = min.z; z < max.z; z++)
        {
            for (u32 y = min.y; y < max.y; y++)
            {
                for (u32 x = min.x; x < max.x; x++)
                {
                    voxels[voxelCount++] = voxel;
                    voxelMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));
                }
            }
        }
        ASSERT(voxelMask != 0);
        return insertVoxels({ voxels.data(), voxelCount }, voxelMask);
    }
}


void SVTree::modify(uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& srcVoxels)
{
    u32 scale = pow4(level);

    SVNode node = m_nodes[nodePtr];
    if (!node.IsLeaf)
    {
        Vec3u localMin = glm::min(min / scale, Vec3u(3));
        Vec3u localMax = glm::min((max - 1u) / scale, Vec3u(3)) + 1u;

        u64 modifyMask = 0;
        for (u32 z = localMin.z; z < localMax.z; z++)
            for (u32 y = localMin.y; y < localMax.y; y++)
                for (u32 x = localMin.x; x < localMax.x; x++)
                    modifyMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));


        if ((node.ChildMask & modifyMask) == modifyMask)
        {
            /* Every node that has to be modified already exists */

            for (u32 z = localMin.z; z < localMax.z; z++)
            {
                for (u32 y = localMin.y; y < localMax.y; y++)
                {
                    for (u32 x = localMin.x; x < localMax.x; x++)
                    {
                        u32 index = SVNode::Index1DFrom3D({ x, y, z });

                        Vec3u newMin = glm::max(min, Vec3u(x, y, z) * scale) & (scale - 1);
                        Vec3u newMax = ((glm::min(max, (Vec3u(x, y, z) + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                        if ((modifyMask >> index) & 1)
                        {
                            Vec3u iMin = glm::max(min, Vec3u(x, y, z) * scale);
                            Vec3u newOffset = offset + iMin - min;//offset + scale * (Vec3u(x, y, z) - localMin);

                            modify(node.GetChildPtr(index), level - 1, newMin, newMax, newOffset, srcVoxels);
                        }
                    }
                }
            }
        }
        else
        {
            /* Some of the nodes have to be created */

            std::array<SVNode, 64> children{};
            u32 srcIndex = 0;
            u32 dstIndex = 0;
            u64 nodeMask = node.ChildMask;

            for (u32 bit = 0; bit < 64; bit++)
            {
                Vec3u pos = (bit >> Vec3u(0, 2, 4)) & 3u;

                Vec3u newMin = glm::max(min, pos * scale) & (scale - 1);
                Vec3u newMax = ((glm::min(max, (pos + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                Vec3u iMin = glm::max(min, pos * scale);
                Vec3u newOffset = offset + iMin - min;//offset + scale * (pos - localMin);
                
                if (node.IsOccupied(bit))
                {
                    uintptr_t childPtr = node.ChildrenPtr + srcIndex++;
                    if ((modifyMask >> bit) & 1)
                    {
                        modify(childPtr, level - 1, newMin, newMax, newOffset, srcVoxels);
                    }
                    children[dstIndex++] = m_nodes[childPtr];
                }
                else if ((modifyMask >> u64(bit)) & 1)
                {
                    SVNode childNode = create(level - 1, newMin, newMax, newOffset, srcVoxels);
                    if (!childNode.IsEmpty())
                    {
                        nodeMask |= (1ull << u64(bit));
                        children[dstIndex++] = childNode;
                    }
                }
            }

            m_nodes[nodePtr] = insertNodes({ children.data(), dstIndex }, nodeMask);
        }
    }
    else
    {
        u64 modifyMask = 0;
        for (u32 z = min.z; z < max.z; z++)
        {
            for (u32 y = min.y; y < max.y; y++)
            {
                for (u32 x = min.x; x < max.x; x++)
                {
                    if (srcVoxels.Get(Vec3u(x, y, z) - min + offset).idx != 0)
                        modifyMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));
                }
            }
        }

        if (node.ChildMask == modifyMask)
        {
            u32 dstIndex = 0;

            for (u32 bit = 0; bit < 64; bit++)
            {
                if (node.IsOccupied(bit))
                {
                    Vec3u srcIndex = ((bit >> Vec3u(0, 2, 4)) & 3u) - min;
                    m_voxels[node.ChildrenPtr + dstIndex] = srcVoxels.Get(srcIndex + offset);
                }
            }
            LOG_WARNING("EZ");
        }
        else if (modifyMask != 0)
        {
            std::array<Voxel, 64> voxels{};
            u32 dstIndex = 0;

            for (u32 bit = 0; bit < 64; bit++)
            {
                if ((modifyMask >> bit) & 1)
                {
                    Vec3u srcIndex = ((bit >> Vec3u(0, 2, 4)) & 3u) - min;
                    voxels[dstIndex++] = srcVoxels.Get(srcIndex + offset);
                }
                else if ((node.ChildMask >> bit) & 1)
                {
                    voxels[dstIndex++] = m_voxels[node.GetChildPtr(bit)]; 
                }
            }

            m_nodes[nodePtr] = insertVoxels({ voxels.data(), dstIndex }, node.ChildMask | modifyMask);
        }
    }
}

// IMPORTANT: may return an empty node
SVNode SVTree::create(u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& srcVoxels)
{
    u32 scale = pow4(level);

    if (level > 0)
    {
        u64 nodeMask = 0;
        uint nodeCount = 0;
        std::array<SVNode, 64> nodes{};

        Vec3u localMin = glm::min(min / scale, Vec3u(3));
        Vec3u localMax = glm::min((max - 1u) / scale, Vec3u(3)) + 1u;

        for (u32 z = localMin.z; z < localMax.z; z++)
        {
            for (u32 y = localMin.y; y < localMax.y; y++)
            {
                for (u32 x = localMin.x; x < localMax.x; x++)
                {
                    Vec3u newMin = glm::max(min, Vec3u(x, y, z) * scale) & (scale - 1);
                    Vec3u newMax = ((glm::min(max, (Vec3u(x, y, z) + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                    Vec3u iMin = glm::max(min, Vec3u(x, y, z) * scale);
                    Vec3u newOffset = offset + iMin - min;//offset + scale * (Vec3u(x, y, z) - localMin);
                    
                    SVNode childNode = create(level - 1, newMin, newMax, newOffset, srcVoxels);
                    if (!childNode.IsEmpty())
                    {
                        nodes[nodeCount++] = childNode;
                        nodeMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));
                    }
                }
            }
        }
        return insertNodes({ nodes.data(), nodeCount }, nodeMask);
    }
    else
    {
        u64 voxelMask = 0;
        uint voxelCount = 0;
        std::array<Voxel, 64> voxels{};

        for (u32 z = min.z; z < max.z; z++)
        {
            for (u32 y = min.y; y < max.y; y++)
            {
                for (u32 x = min.x; x < max.x; x++)
                {
                    Voxel voxel = srcVoxels.Get(Vec3u(x, y, z) - min + offset);
                    
                    if (voxel)
                    {
                        voxels[voxelCount++] = voxel;
                        voxelMask |= 1ull << u64(SVNode::Index1DFrom3D({ x, y, z }));
                    }
                }
            }
        }
        return insertVoxels({ voxels.data(), voxelCount }, voxelMask);
    }
}


bool SVTree::Dump(std::ostream& output)
{
    output << "SVT1";

    IO::WriteU32(output, m_dimensions.x);
    IO::WriteU32(output, m_dimensions.y);
    IO::WriteU32(output, m_dimensions.z);
    IO::WriteU32(output, m_numLevels);
    IO::WriteU64(output, u64(m_nodes.size()));
    IO::WriteU64(output, u64(m_voxels.size()));

    output.write((char*)m_nodes.data(), m_nodes.size() * sizeof(m_nodes[0]));
    output.write((char*)m_voxels.data(), m_voxels.size() * sizeof(m_voxels[0]));

    return !output.fail();
}


bool SVTree::Load(std::istream& input)
{
    std::string signature;
    signature += input.get();
    signature += input.get();
    signature += input.get();
    signature += input.get();

    if (signature != "SVT1")
        return false;


    u64 nodeCount = 0;
    u64 voxelCount = 0;

    if (!(
        IO::ReadU32(input, &m_dimensions.x) &&
        IO::ReadU32(input, &m_dimensions.y) && 
        IO::ReadU32(input, &m_dimensions.z) &&
        IO::ReadU32(input, &m_numLevels) &&
        IO::ReadU64(input, &nodeCount) &&
        IO::ReadU64(input, &voxelCount)
    ))
        return false;


    if (nodeCount == 0 || voxelCount == 0)
        return false;
    
    m_nodes.resize(nodeCount);
    if (!IO::ReadArray(input, &m_nodes[0], nodeCount, sizeof(m_nodes[0])))
        return false;

    m_voxels.resize(voxelCount);
    if (!IO::ReadArray(input, &m_voxels[0], voxelCount, sizeof(m_voxels[0])))
        return false;

    return true;
}


void SVTree::Subtract(Vec3u min, const VoxelArray& mask)
{
    Vec3u max = min + mask.Size;
 
    ASSERT(glm::all(glm::greaterThanEqual(min, Vec3u(0))));
    ASSERT(glm::all(glm::lessThanEqual(max, Vec3u(pow4(m_numLevels)))));

    subtract(UINTPTR_MAX, 0, m_numLevels - 1, min, max, Vec3u(0), mask);
}


bool SVTree::subtract(uintptr_t parentPtr, uintptr_t nodePtr, u32 level, Vec3u min, Vec3u max, Vec3u offset, const VoxelArray& mask)
{
    SVNode node = m_nodes[nodePtr];

    if (level > 0)
    {
        u32 scale = pow4(level);

        Vec3u localMin = glm::min(min / scale, Vec3u(3));
        Vec3u localMax = glm::min((max - 1u) / scale, Vec3u(3)) + 1u;

        u32 originalChildrenCount = node.GetChildrenCount();
        bool anyRemoved = false;

        for (u32 z = localMin.z; z < localMax.z; z++)
        {
            for (u32 y = localMin.y; y < localMax.y; y++)
            {
                for (u32 x = localMin.x; x < localMax.x; x++)
                {
                    u32 index = SVNode::Index1DFrom3D({ x, y, z });

                    if (node.IsOccupied(index))
                    {
                        Vec3u newMin = glm::max(min, Vec3u(x, y, z) * scale) & (scale - 1);
                        Vec3u newMax = ((glm::min(max, (Vec3u(x, y, z) + 1u) * scale) - 1u) & (scale - 1)) + 1u;

                        Vec3u iMin = glm::max(min, Vec3u(x, y, z) * scale);
                        Vec3u newOffset = offset + iMin - min;
                        
                        uintptr_t childPtr = node.GetChildPtr(index);
                        if (subtract(nodePtr, childPtr, level - 1, newMin, newMax, newOffset, mask))
                        {
                            anyRemoved = true;
                            m_nodes[nodePtr].ChildMask &= ~(1ull << u64(index));
                            //deleteChild(nodePtr, index);
                        }
                    }
                }
            }
        }

        if (anyRemoved)
        {
            // Remove all empty nodes
            u32 srcPtr = node.ChildrenPtr;
            u32 dstPtr = srcPtr;
            while (srcPtr < node.ChildrenPtr + originalChildrenCount)
            {
                if (m_nodes[srcPtr].IsEmpty())
                    srcPtr++;
                else
                    m_nodes[dstPtr++] = m_nodes[srcPtr++]; 
            }
        }
    }
    else
    {
        u32 originalVoxelCount = node.GetChildrenCount();

        for (u32 z = min.z; z < max.z; z++)
        {
            for (u32 y = min.y; y < max.y; y++)
            {
                for (u32 x = min.x; x < max.x; x++)
                {
                    Vec3u dstPos = Vec3u(x, y, z);
                    u32 dstIndex = SVNode::Index1DFrom3D(dstPos);

                    Vec3u srcPos = Vec3u(x, y, z) - min + offset;
                    //u32 srcIndex = 0;
                    
                    if (node.IsOccupied(dstIndex) && mask.Get(srcPos))
                    {
                        m_nodes[nodePtr].ChildMask &= ~(1ull << u64(dstIndex));

                        // Mark as "deleted"; remove later
                        m_voxels[node.GetChildPtr(dstIndex)].idx = 0;
                    }
                }
            }
        }

        // Remove all zero voxels
        u32 srcPtr = node.ChildrenPtr;
        u32 dstPtr = srcPtr;
        while (srcPtr < node.ChildrenPtr + originalVoxelCount)
        {
            if (m_voxels[srcPtr].idx == 0)
                srcPtr++;
            else
                m_voxels[dstPtr++] = m_voxels[srcPtr++]; 
        }
    }



    return m_nodes[nodePtr].IsEmpty();
}


void SVTree::deleteChild(uintptr_t nodePtr, u32 childIndex)
{
    SVNode node = m_nodes[nodePtr];
    if (!node.IsLeaf)
    {
        uintptr_t childPtr = node.GetChildPtr(childIndex);
        uintptr_t childrenCount = node.GetChildrenCount();
        uintptr_t moveCount = childrenCount - (childPtr - node.ChildrenPtr) - 1;

        memmove(m_nodes.data() + childPtr, m_nodes.data() + childPtr + 1, moveCount * sizeof(SVNode));

        m_nodes[nodePtr].ChildMask &= ~(1ull << u64(childIndex));
    }
}