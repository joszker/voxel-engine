

layout(std430, buffer_reference, buffer_reference_align = 4) buffer VHKeys
{
    uint at[];
};

layout(std430, buffer_reference, buffer_reference_align = 16) buffer VHValues
{
    vec4 at[];
};


struct VoxelHashMap
{
    VHKeys keys;
    VHValues values;
};

#define VHM_EMPTY_KEY       0xffffffff
#define VHM_BUFFER_SIZE     16777216       


uint Hash(uint k, uint n)
{
    k ^= k >> 16;
    k *= 0x85ebca6b;
    k ^= k >> 13;
    k *= 0xc2b2ae35;
    k ^= k >> 16;
    return k & (n - 1);
}



void VHM_AddSample(VoxelHashMap vhm, uint key, vec3 radianceSample)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 1024; i++)
    {
        uint prevKey = atomicCompSwap(vhm.keys.at[slot], VHM_EMPTY_KEY, key);

        if (prevKey == VHM_EMPTY_KEY || prevKey == key)
        {
            atomicAdd(vhm.values.at[slot].r, radianceSample.r);
            atomicAdd(vhm.values.at[slot].g, radianceSample.g);
            atomicAdd(vhm.values.at[slot].b, radianceSample.b);
            atomicAdd(vhm.values.at[slot].w, 1.0);

            return;
        }

        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }    
}


vec4 VHM_Lookup(VoxelHashMap vhm, uint key)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 256; i++)
    {
        if (vhm.keys.at[slot] == key)
        {
            return vhm.values.at[slot];
        }
        if (vhm.keys.at[slot].x == VHM_EMPTY_KEY)
        {
            return vec4(0.0);
        }

        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }
    return vec4(0.0);
}