struct VHData
{
    vec4 radiance;          // accumulated
    float sun;              // accumulated sun light
    uint frame;             // last update frame
    uint gen;
    float ao;
};

layout(std430, buffer_reference, buffer_reference_align = 4) buffer VHKeys
{
    uint at[];
};

layout(std430, buffer_reference, buffer_reference_align = 16) buffer VHValues
{
    VHData at[];
};


struct VoxelHashMap
{
    VHKeys keys;
    VHValues values;
};

#define VHM_EMPTY_KEY       0xffffffff

#define VHM_BUFFER_SIZE     33554432
//#define VHM_BUFFER_SIZE     16777216

#define VHM_EMPTY_VALUE   VHData(vec4(0,1,0,1), 0, 0, 0, 0)

#define VHM_SAMPLE_LIMIT    524288
//#define VHM_SAMPLE_LIMIT    16384


uint Hash(uint k, uint n)
{
    k ^= k >> 16;
    k *= 0x85ebca6b;
    k ^= k >> 13;
    k *= 0xc2b2ae35;
    k ^= k >> 16;
    return k & (n - 1);
}



void VHM_AddSample(VoxelHashMap vhm, uint key, vec4 radianceSample, float sun, uint frame)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 1024; i++)
    {
        uint prevKey = atomicCompSwap(vhm.keys.at[slot], VHM_EMPTY_KEY, key);

        if (prevKey == VHM_EMPTY_KEY || prevKey == key)
        {
            if (vhm.values.at[slot].radiance.w < VHM_SAMPLE_LIMIT)
            {
                atomicAdd(vhm.values.at[slot].radiance.r, radianceSample.r);
                atomicAdd(vhm.values.at[slot].radiance.g, radianceSample.g);
                atomicAdd(vhm.values.at[slot].radiance.b, radianceSample.b);
                atomicAdd(vhm.values.at[slot].radiance.w, radianceSample.w);

                atomicAdd(vhm.values.at[slot].sun, sun);
            }
            atomicExchange(vhm.values.at[slot].frame, frame);

            return;
        }
        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }    
}


VHData VHM_Lookup(VoxelHashMap vhm, uint key)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 256; i++)
    {
        if (vhm.keys.at[slot] == key)
        {
            return vhm.values.at[slot];
        }
        if (vhm.keys.at[slot] == VHM_EMPTY_KEY)
        {
            return VHM_EMPTY_VALUE;
        }

        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }
    return VHM_EMPTY_VALUE;
}


void VHM_Copy(VoxelHashMap vhm, uint key, VHData data)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 1024; i++)
    {
        uint prevKey = atomicCompSwap(vhm.keys.at[slot], VHM_EMPTY_KEY, key);

        if (prevKey == VHM_EMPTY_KEY || prevKey == key)
        {
            vhm.values.at[slot] = data;
            return;
        }
        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }    
}