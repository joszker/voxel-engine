#include "SVTree.h"
#include <limits>
#include <thread>

class SVBuilder
{
private:
    static constexpr u32 INVALID_INDEX = UINT32_MAX;


    /*
        A level contains only the nodes that actually exist.
        Nodes are ordered by their parent at the next level.
        All children belonging to one parent are contiguous.
    */
    struct Level
    {
        std::vector<SVNode> nodes;
        std::vector<Vec3u> coords;      // Spatial coordinate of every node. nodes[i] corresponds to coords[i].

        /*
            Dense spatial lookup.
            `lookup[x + y * size.x + z * size.x * size.y]` gives the index into nodes/coords, or INVALID_INDEX.
        */
        std::vector<uint32_t> lookup;   

        Vec3u size = { 0, 0, 0 };       //  Number of possible node positions in this level.
    };

    struct ThreadOutput
    {
        std::vector<SVNode> nodes;
        std::vector<Vec3u> coords;
        std::vector<Voxel> voxels;
    };

public:
    explicit SVBuilder(
        uint32_t threadCount = 0)
    {
        if (threadCount == 0)
        {
            threadCount =
                std::max(
                    1u,
                    std::thread::hardware_concurrency());
        }

        m_threadCount = threadCount;
    }

    SVTree Build(
        const VoxelArray& array);

private:
    u32 m_threadCount;

private:
    /*
        Return the number of possible nodes in a level.
    */
    static size_t LevelSize(const Level& level)
    {
        return size_t(level.size.x) * level.size.y * level.size.z;
    }

    /*
        Number of threads actually used for a particular workload.
    */
    uint32_t EffectiveThreadCount(size_t workCount) const
    {
        if (workCount == 0) return 1;

        return std::max(1u, std::min(m_threadCount, u32(workCount)));
    }

    /*
        Divide [0, count) into contiguous ranges.

        Keeping ranges contiguous is important because our spatial
        ordering is encoded in the order in which work items are
        processed.
    */
    std::vector<std::pair<size_t, size_t>> MakeRanges(size_t count) const
    {
        const uint32_t threadCount = EffectiveThreadCount(count);

        std::vector<std::pair<size_t, size_t>> ranges(threadCount);

        const size_t base = count / threadCount;
        const size_t remainder = count % threadCount;

        size_t begin = 0;
        for (uint32_t i = 0; i < threadCount; i++)
        {
            const size_t size = base + (i < remainder ? 1 : 0);

            ranges[i] = { begin, begin + size };
            begin += size;
        }

        return ranges;
    }

    /* Build the leaf level from the voxel array. */
    Level BuildLeafLevel(const VoxelArray& array, u32 scale, std::vector<Voxel>& outputVoxels);

    /* Build one internal level from the immediately lower level. */
    Level BuildParentLevel(const Level& children);

    /* Construct the dense spatial lookup for a level. */
    static void BuildLookup(Level& level);
};