struct VHData
{
    vec4 radiance;
    float sun;
    uint frame;             // last update frame
    uint gen;
    uint data2;
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
#define VHM_BUFFER_SIZE     16777216


//#define VHM_SAMPLE_LIMIT    4096

#define VHM_SAMPLE_LIMIT    524288
//#define VHM_SAMPLE_LIMIT    16384
//#define VHM_SAMPLE_LIMIT    1024
//#define VHM_SAMPLE_LIMIT    16384
//#define VHM_SAMPLE_LIMIT      64


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
                //float gain = mix(1.0, 1.00024420024, float(sampleCount > VHM_SAMPLE_LIMIT));
                //radianceSample *= gain;
                //sun *= gain;

                atomicAdd(vhm.values.at[slot].radiance.r, radianceSample.r);
                atomicAdd(vhm.values.at[slot].radiance.g, radianceSample.g);
                atomicAdd(vhm.values.at[slot].radiance.b, radianceSample.b);
                atomicAdd(vhm.values.at[slot].radiance.w, radianceSample.w);

                atomicAdd(vhm.values.at[slot].sun, sun);
            }
            atomicExchange(vhm.values.at[slot].frame, frame);

            return;
        }

        /*
        if (frame - vhm.values.at[slot].frame > 2000)
        {
            // Reuse slot if it hasn't been used for 2000 frames.

            atomicExchange(vhm.keys.at[slot], key);
            atomicExchange(vhm.values.at[slot].radiance.r, radianceSample.r);
            atomicExchange(vhm.values.at[slot].radiance.g, radianceSample.g);
            atomicExchange(vhm.values.at[slot].radiance.b, radianceSample.b);
            atomicExchange(vhm.values.at[slot].radiance.w, 1.0);
            atomicExchange(vhm.values.at[slot].sun, sun);
            atomicExchange(vhm.values.at[slot].frame, frame);

            return;
        }*/

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
            return VHData(vec4(0,1,0,1), 0, 0, 0, 0);
            //return VHData(vec4[](vec4(0,1,0,1), vec4(0)), 0, 0, 0, 0);
        }

        slot += 1;
        slot &= VHM_BUFFER_SIZE - 1;
    }
    return VHData(vec4(0,1,0,1), 0, 0, 0, 0);
    //return VHData(vec4[](vec4(1,0,0,1), vec4(0)), 0, 0, 0, 0);
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


/*
vec4 VHM_QuickLookup(VoxelHashMap vhm, uint key)
{
    uint slot = Hash(key, VHM_BUFFER_SIZE);

    for (uint i = 0; i < 64; i++)
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
*/