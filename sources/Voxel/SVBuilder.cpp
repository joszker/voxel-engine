#include "SVBuilder.h"
#include "Base/Log.h"




static u32 LinearIndex(Vec3u coord, Vec3u size)
{
    return coord.z * size.x * size.y + coord.y * size.x + coord.x;
}

static u32 ChildIndex(u32 x, u32 y, u32 z)
{
    return x | (y << 2) | (z << 4);
}

/*
    Check the 31-bit pointer constraint.
*/
static void CheckPointer(size_t value)
{
    if (value >= (size_t(1) << 31))
    {
        ASSERT(!"SVTree exceeds 31-bit ChildrenPtr range");
    }
}

/*
    Given a level coordinate, return its ordering key.

    The key is:

        parentLinearIndex * 64 + childIndex

    This is the crucial part of the implementation.

    It means:

        parent 0 -> children 0..63
        parent 1 -> children 0..63
        ...

    so siblings are always contiguous.
*/
static u64 OrderingKey(Vec3u coord, u32 sizeX, u32 sizeY)
{
    const u32 parentX =
        coord.x / 4;

    const u32 parentY =
        coord.y / 4;

    const u32 parentZ =
        coord.z / 4;

    const u32 childX =
        coord.x & 3;

    const u32 childY =
        coord.y & 3;

    const u32 childZ =
        coord.z & 3;

    const u32 parentSizeX =
        (sizeX + 3) / 4;

    const u32 parentSizeY =
        (sizeY + 3) / 4;

    const u64 parentIndex =
        u64(parentZ) *
        parentSizeX *
        parentSizeY +
        u64(parentY) *
        parentSizeX +
        parentX;

    const u32 childIndex =
        ChildIndex(
            childX,
            childY,
            childZ);

    return parentIndex * 64 + childIndex;
}

/*
    Convert an ordering key back to a coordinate.

    `sizeX/sizeY` are the dimensions of the level.
*/
static Vec3u CoordinateFromOrderingKey(u64 key, u32 sizeX, u32 sizeY)
{
    const u32 childIndex =
        u32(key & 63);

    const u64 parentIndex =
        key >> 6;

    const u32 parentSizeX =
        (sizeX + 3) / 4;

    const u32 parentSizeY =
        (sizeY + 3) / 4;

    const u32 parentX =
        u32(
            parentIndex %
            parentSizeX);

    const u64 parentYZ =
        parentIndex /
        parentSizeX;

    const u32 parentY =
        u32(
            parentYZ %
            parentSizeY);

    const u32 parentZ =
        u32(
            parentYZ /
            parentSizeY);

    const u32 childX =
        childIndex & 3;

    const u32 childY =
        (childIndex >> 2) & 3;

    const u32 childZ =
        (childIndex >> 4) & 3;

    return {
        parentX * 4 + childX,
        parentY * 4 + childY,
        parentZ * 4 + childZ
    };
}


void SVBuilder::BuildLookup(Level& level)
{
    const size_t count = LevelSize(level);

    level.lookup.assign(count, INVALID_INDEX);

    for (u32 i = 0; i < level.nodes.size(); i++)
    {
        const u32 index = LinearIndex(level.coords[i], level.size);

        ASSERT(index < level.lookup.size());
        ASSERT(level.lookup[index] == INVALID_INDEX);

        level.lookup[index] = i;
    }
}

SVBuilder::Level SVBuilder::BuildLeafLevel(const VoxelArray& array, u32 scale, std::vector<Voxel>& outputVoxels)
{
    Level level;

    /* A leaf contains a 4x4x4 voxel region. */
    level.size = Vec3u(scale / 4);

    const size_t possibleLeaves = LevelSize(level);

    /*
        The work items are NOT simply linear x/y/z order.

        They are ordered:

            parent 0:
                child 0
                child 1
                ...
                child 63

            parent 1:
                child 0
                ...

        This guarantees sibling leaves are contiguous.
    */

    const u32 parentSizeX = (level.size.x + 3u) / 4u;
    const u32 parentSizeY = (level.size.y + 3u) / 4u;
    const u32 parentSizeZ = (level.size.z + 3u) / 4u;

    const size_t workCount = size_t(parentSizeX) * parentSizeY * parentSizeZ * 64;

    auto ranges = MakeRanges(workCount);
    std::vector<ThreadOutput> outputs(ranges.size());

    std::vector<std::thread> threads;
    threads.reserve(ranges.size());

    for (u32 threadIndex = 0; threadIndex < ranges.size(); threadIndex++)
    {
        threads.emplace_back([&, threadIndex]()
        {
            auto [begin, end] = ranges[threadIndex];

            ThreadOutput& output = outputs[threadIndex];

            for (size_t workIndex = begin; workIndex < end; workIndex++)
            {
                /*
                if (workIndex == begin)
                    LOG_INFO("Thread #{}: 0%", threadIndex);

                if (workIndex == begin + (end - begin) / 4)
                    LOG_INFO("Thread #{}: 25%", threadIndex);

                if (workIndex == begin + (end - begin) / 2)
                    LOG_INFO("Thread #{}: 50%", threadIndex);

                if (workIndex == begin + 3 * (end - begin) / 4)
                    LOG_INFO("Thread #{}: 75%", threadIndex);*/

                /* Decode the spatial coordinate from the grouped ordering. */
                const Vec3u coord = CoordinateFromOrderingKey(
                    workIndex,
                    level.size.x,
                    level.size.y
                );

                /*
                    Ignore coordinates outside the actual leaf grid. 
                    This can happen because the last 4x4x4 parent group may not be full.

                    In our particular scale construction the grid is normally a power of 4, 
                    but keeping this check makes the code robust.
                */
                if (coord.x >= level.size.x || coord.y >= level.size.y || coord.z >= level.size.z)
                {
                    continue;
                }

                const u32 voxelPtr = u32(output.voxels.size());
                u64 mask = 0;

                const u32 baseX = coord.x * 4;
                const u32 baseY = coord.y * 4;
                const u32 baseZ = coord.z * 4;

                for (u32 z = 0; z < 4; z++)
                {
                    const u32 vz = baseZ + z;
                    if (vz >= array.Size.z) continue;

                    for (u32 y = 0; y < 4; y++)
                    {
                        const u32 vy = baseY + y;
                        if (vy >= array.Size.y) continue;

                        for (u32 x = 0; x < 4; x++)
                        {
                            const u32 vx = baseX + x;
                            if (vx >= array.Size.x) continue;

                            const size_t index = 
                                size_t(vz) * array.Size.x * array.Size.y +
                                size_t(vy) * array.Size.x +
                                size_t(vx);

                            const Voxel voxel = array.Data[index];
                            if (voxel.idx == 0) continue;

                            mask |= u64(1) << (ChildIndex(x, y, z));
                            output.voxels.push_back(voxel);
                        }
                    }
                }

                if (mask == 0)
                    continue;

                SVNode node{
                    .IsLeaf = 1,
                    .ChildrenPtr = voxelPtr,    // IMPORTANT: Temporary pointer into this thread's voxel buffer.
                    .ChildMask = mask
                };
                output.nodes.push_back(node);
                output.coords.push_back(coord);
            }
            //LOG_INFO("Thread #{}: 100%", threadIndex);
        });
    }
    for (auto& thread : threads)
        thread.join();

    //LOG_INFO("Merge outputs");
    /*
        Merge thread outputs.

        Since each thread processed a contiguous section of the global ordering, 
        concatenating in thread order preserves the required sibling grouping.
    */
    size_t totalNodes = 0;
    size_t totalVoxels = 0;

    for (const ThreadOutput& output : outputs)
    {
        totalNodes += output.nodes.size();
        totalVoxels += output.voxels.size();
    }

    level.nodes.reserve(totalNodes);
    level.coords.reserve(totalNodes);

    outputVoxels.reserve(outputVoxels.size() + totalVoxels);

    for (ThreadOutput& output : outputs)
    {
        const u32 voxelBase = u32(outputVoxels.size());

        CheckPointer(outputVoxels.size() + output.voxels.size());
        outputVoxels.insert(outputVoxels.end(), output.voxels.begin(), output.voxels.end());

        for (size_t i = 0; i < output.nodes.size(); i++)
        {
            SVNode node = output.nodes[i];

            /* IMPORTANT: Convert thread-local voxel pointer into a global voxel pointer. */
            node.ChildrenPtr += voxelBase;

            level.nodes.push_back(node);
            level.coords.push_back(output.coords[i]);
        }
    }

    //LOG_INFO("Build lookup");
    BuildLookup(level);

    return level;
}


SVBuilder::Level SVBuilder::BuildParentLevel(const Level& children)
{
    Level level{};
    level.size = (children.size + 3u) / 4u;

    /* Number of possible parent positions. */
    const size_t possibleParents = LevelSize(level);

    /*
        We enumerate parents in the same grouped ordering:

            grandparent 0:
                parent 0
                parent 1
                ...
                parent 63

            grandparent 1:
                ...
    */

    const u32 grandparentSizeX = (level.size.x + 3u) / 4u;
    const u32 grandparentSizeY = (level.size.y + 3u) / 4u;
    const u32 grandparentSizeZ = (level.size.z + 3u) / 4u;

    const size_t workCount = size_t(grandparentSizeX) * grandparentSizeY * grandparentSizeZ * 64;

    auto ranges = MakeRanges(workCount);
    std::vector<ThreadOutput> outputs(ranges.size());

    std::vector<std::thread> threads;
    threads.reserve(ranges.size());

    for (u32 threadIndex = 0; threadIndex < ranges.size(); ++threadIndex)
    {
        threads.emplace_back([&, threadIndex]()
        {
            auto [begin, end] = ranges[threadIndex];

            ThreadOutput& output = outputs[threadIndex];

            for (size_t workIndex = begin; workIndex < end; workIndex++)
            {
                const Vec3u parentCoord = CoordinateFromOrderingKey(workIndex, level.size.x, level.size.y);

                if (parentCoord.x >= level.size.x || parentCoord.y >= level.size.y || parentCoord.z >= level.size.z)
                {
                    continue;
                }

                u64 mask = 0;
                u32 firstChild = INVALID_INDEX;

                /* Examine 4x4x4 children. */
                for (u32 z = 0; z < 4; z++)
                {
                    const u32 cxBase = parentCoord.x * 4;
                    const u32 cyBase = parentCoord.y * 4;
                    const u32 czBase = parentCoord.z * 4;

                    const u32 cz = czBase + z;
                    if (cz >= children.size.z) continue;

                    for (u32 y = 0; y < 4; ++y)
                    {
                        const u32 cy = cyBase + y;
                        if (cy >= children.size.y) continue;

                        for (u32 x = 0; x < 4; x++)
                        {
                            const u32 cx = cxBase + x;
                            if (cx >= children.size.x) continue;

                            const u32 lookupIndex = LinearIndex(Vec3u(cx, cy, cz), children.size);

                            const u32 childIndex = children.lookup[lookupIndex];
                            if (childIndex == INVALID_INDEX) continue;

                            mask |= u64(1) << (ChildIndex(x, y, z));

                            if (firstChild == INVALID_INDEX)
                                firstChild = childIndex;
                        }
                    }
                }

                if (mask == 0)
                    continue;

                // NOTE: First occupied child is the beginning of the complete child range.
                SVNode node{
                    .IsLeaf = 0,
                    .ChildrenPtr = firstChild,  // This points into the PREVIOUS LEVEL.
                    .ChildMask = mask,
                };
                output.nodes.push_back(node);
                output.coords.push_back(parentCoord);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    /*
        Merge thread output.

        The ranges correspond to the global parent ordering, so this preserves:

            parent-group 0
            parent-group 1
            ...
    */
    size_t totalNodes = 0;

    for (const ThreadOutput& output : outputs)
        totalNodes += output.nodes.size();

    level.nodes.reserve(totalNodes);
    level.coords.reserve(totalNodes);

    for (const ThreadOutput& output : outputs)
    {
        level.nodes.insert(level.nodes.end(), output.nodes.begin(), output.nodes.end());
        level.coords.insert(level.coords.end(), output.coords.begin(), output.coords.end());
    }

    BuildLookup(level);

    return level;
}


SVTree SVBuilder::Build(const VoxelArray& array)
{
    SVTree tree;

    tree.m_dimensions = array.Size;

    /* Handle empty array. */
    if (array.Size.x == 0 || array.Size.y == 0 || array.Size.z == 0)
    {
        tree.m_numLevels = 1;

        tree.m_nodes.resize(1);

        tree.m_nodes[0].IsLeaf = 1;
        tree.m_nodes[0].ChildrenPtr = 0;
        tree.m_nodes[0].ChildMask = 0;

        return tree;
    }

    /*
        scale = next power of two >= largest dimension, rounded up to an even exponent so scale is a power of 4.
    */
    const u32 maxDimension = std::max({ array.Size.x, array.Size.y, array.Size.z });

    u32 scale = 1;
    u32 exponent = 0;
    while (scale < maxDimension)
    {
        scale <<= 1;
        ++exponent;
    }
    if (exponent & 1u)
    {
        scale <<= 1;
        ++exponent;
    }
    if (scale < 4)
    {
        scale = 4;
        exponent = 2;
    }
    const u32 numLevels = exponent / 2;

    ASSERT(numLevels >= 1);
    ASSERT(numLevels <= 16);

    tree.m_numLevels = numLevels;

    /*
        levels[0] = leaf nodes
        levels[1] = parents of leaves
        levels[2] = parents of level 1
        ...
        levels[N-1] = root
    */
    std::vector<Level> levels;

    levels.reserve(numLevels);

    /* Voxel storage is only generated at the leaf level */
    std::vector<Voxel> voxels;

    LOG_TRACE("Building svt level {}/{} ...", levels.size() + 1, numLevels);
    Level leaves = BuildLeafLevel(array, scale, voxels);

    if (leaves.nodes.empty()) /* Volume is completely empty.*/
    {
        tree.m_nodes.resize(1);

        tree.m_nodes[0].IsLeaf = 1;
        tree.m_nodes[0].ChildrenPtr = 0;
        tree.m_nodes[0].ChildMask = 0;

        tree.m_voxels.clear();

        return tree;
    }
    
    levels.push_back(std::move(leaves));

    /* Build parent levels until only one node remains. */
    while (levels.back().nodes.size() > 1)
    {
        LOG_TRACE("Building svt level {}/{} ...", levels.size() + 1, numLevels);

        Level parent = BuildParentLevel(levels.back());

        /* There should always be a parent if there is more than one node at the previous level. */
        ASSERT(!parent.nodes.empty());

        levels.push_back(std::move(parent));
    }

    /*
        We now have:

            levels[0] = leaves
            levels[1] = ...
            ...
            levels[N] = root

        Every node's ChildrenPtr points into the immediately preceding level.

        We now concatenate the levels into the final m_nodes array, placing the root first.
    */

    std::vector<size_t> levelBase(levels.size());
    size_t totalNodes = 0;

    // NOTE: Root level first.
    for (size_t level = levels.size(); level-- > 0;)
    {
        levelBase[level] = totalNodes;
        totalNodes += levels[level].nodes.size();
    }

    CheckPointer(totalNodes);

    tree.m_nodes.resize(totalNodes);

    /* Copy levels into the final node array. */
    for (size_t level = levels.size(); level-- > 0;)
    {
        const size_t destinationBase = levelBase[level];

        for (size_t i = 0; i < levels[level].nodes.size(); i++)
        {
            SVNode node = levels[level].nodes[i];

            if (node.IsLeaf)
            {
                /* Leaf ChildrenPtr already points into the final voxel array. */
            }
            else
            {
                /*
                    ChildrenPtr currently points into the immediately preceding LEVEL.

                    Convert it to an index in final m_nodes.
                */
                node.ChildrenPtr += u32(levelBase[level - 1]);
            }
            tree.m_nodes[destinationBase + i] = node;
        }
    }

    tree.m_voxels = std::move(voxels);
    return tree;
}

