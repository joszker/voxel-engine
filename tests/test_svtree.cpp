#include "catch.hpp"
#include "Voxel/SVTree.h"




TEST_CASE("empty 0x0x0", "[svt]")
{
    SVTree tree({ {}, Vec3u(0, 0, 0) });

    REQUIRE(tree.GetNodes().size() == 1);
    REQUIRE(tree.GetVoxels().size() == 0);   
}


TEST_CASE("empty 1", "[svt]")
{
    std::vector<Voxel> voxels = { Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0} };

    SVTree tree({ voxels, Vec3u(2, 2, 1) });

    REQUIRE(tree.Get({ 0, 0, 0 }).idx == 0);
    REQUIRE(tree.Get({ 1, 0, 0 }).idx == 0);
    REQUIRE(tree.Get({ 0, 1, 0 }).idx == 0);

    REQUIRE(tree.GetNodes().size() == 1);
    REQUIRE(tree.GetVoxels().size() == 0);   
}


TEST_CASE("1 level", "[svt]")
{
    std::vector<Voxel> voxels = {
        Voxel { 10 }, Voxel { 20 }, Voxel { 30 }, Voxel { 40 },
        Voxel { 11 }, Voxel { 21 }, Voxel { 31 }, Voxel { 41 },
        Voxel { 12 }, Voxel { 22 }, Voxel { 32 }, Voxel { 42 },
        Voxel { 13 }, Voxel { 23 }, Voxel { 33 }, Voxel { 43 },

        Voxel { 50 }, Voxel { 60 }, Voxel { 70 }, Voxel { 80 },
        Voxel { 51 }, Voxel { 61 }, Voxel { 71 }, Voxel { 81 },
        Voxel { 52 }, Voxel { 62 }, Voxel { 72 }, Voxel { 82 },
        Voxel { 53 }, Voxel { 63 }, Voxel { 73 }, Voxel { 83 },
    };

    SVTree tree({ voxels, Vec3u(4, 4, 2) });

    REQUIRE(tree.Get({ 0, 0, 0 }).idx == 10);
    REQUIRE(tree.Get({ 1, 0, 0 }).idx == 20);
    REQUIRE(tree.Get({ 2, 0, 0 }).idx == 30);
    REQUIRE(tree.Get({ 3, 0, 0 }).idx == 40);

    REQUIRE(tree.Get({ 0, 1, 0 }).idx == 11);
    REQUIRE(tree.Get({ 1, 1, 0 }).idx == 21);
    REQUIRE(tree.Get({ 2, 2, 0 }).idx == 32);
    REQUIRE(tree.Get({ 3, 3, 0 }).idx == 43);

    REQUIRE(tree.Get({ 0, 0, 1 }).idx == 50);
    REQUIRE(tree.Get({ 1, 0, 1 }).idx == 60);
    REQUIRE(tree.Get({ 2, 0, 1 }).idx == 70);
    REQUIRE(tree.Get({ 3, 0, 1 }).idx == 80);

    REQUIRE(tree.Get({ 0, 3, 1 }).idx == 53);
    REQUIRE(tree.Get({ 1, 2, 1 }).idx == 62);
    REQUIRE(tree.Get({ 2, 1, 1 }).idx == 71);
    REQUIRE(tree.Get({ 3, 2, 1 }).idx == 82);

    REQUIRE(tree.GetNodes().size() == 1);
    REQUIRE(tree.GetVoxels().size() == 32);
}


TEST_CASE("2 levels")
{
    std::vector<Voxel> voxels;
    voxels.reserve(16 * 16 * 16);

    for (int z = 0; z < 16; z++) 
    {
        for (int y = 0; y < 16; y++) 
        {
            for (int x = 0; x < 16; x++) 
            {
                voxels.push_back(Voxel{ u8((x % 8) * 10 + (y % 8)) });
            }
        }
    }

    SVTree tree({ voxels, Vec3u(16, 16, 16) });


    REQUIRE(tree.Get({ 0, 0, 0 }).idx == 0);
    REQUIRE(tree.Get({ 7, 7, 7 }).idx == 77);
    REQUIRE(tree.Get({ 10, 5, 13 }).idx == 25);
    REQUIRE(tree.Get({ 14, 11, 4 }).idx == 63);
}


TEST_CASE("empty nodes", "[svt]")
{
    std::vector<Voxel> voxels = { 
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},

        Voxel{1}, Voxel{1}, Voxel{1}, Voxel{1},
        Voxel{1}, Voxel{1}, Voxel{1}, Voxel{1},
        Voxel{1}, Voxel{1}, Voxel{1}, Voxel{1},
        Voxel{1}, Voxel{1}, Voxel{1}, Voxel{1},

        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},
        Voxel{0}, Voxel{0}, Voxel{0}, Voxel{0},

        Voxel{0}, Voxel{1}, Voxel{1}, Voxel{1},
        Voxel{1}, Voxel{0}, Voxel{1}, Voxel{1},
        Voxel{1}, Voxel{1}, Voxel{0}, Voxel{1},
        Voxel{1}, Voxel{1}, Voxel{1}, Voxel{0},
    };

    SVTree tree({ voxels, Vec3u(4, 4 * 4, 1) });

    REQUIRE(tree.GetNodes().size() == 3);
    REQUIRE(tree.GetVoxels().size() == 16 + 16 - 4);   
}