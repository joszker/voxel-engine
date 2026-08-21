// Owen-scrambled Sobol
// from: https://www.shadertoy.com/view/dljBzG (Z-Indexed Sobol, playbyan1453 2026-05-30)


#define NativeGL 1

// Most codes are from Blender Cycles
// Sobol table and hash : https://github.com/blender/cycles/blob/main/src/kernel/sample/sobol_burley.h

// Unique number for sobol dimension
const uint PRNG_FILTER = 0u;
const uint PRNG_LENS_TIME = 1u;
// Shade bounce
const uint PRNG_TERMINATE = 0u;
const uint PRNG_LIGHT = 1u;
const uint PRNG_LIGHT_TERMINATE = 2u;
// Surface
const uint PRNG_SURFACE_BSDF = 3u;
const uint PRNG_BOUNCE_NUM = 16u;

uint seed = 0u;

struct hash_seed {
    uint pixelrng;
    uint samples;
};


uint ReverseBits(uint x) {
#if NativeGL
    return bitfieldReverse(x);
#else
    x = ((x & 0xaaaaaaaau) >> 1u) | ((x & 0x55555555u) << 1u);
    x = ((x & 0xccccccccu) >> 2u) | ((x & 0x33333333u) << 2u);
    x = ((x & 0xf0f0f0f0u) >> 4u) | ((x & 0x0f0f0f0fu) << 4u);
    x = ((x & 0xff00ff00u) >> 8u) | ((x & 0x00ff00ffu) << 8u);
    return (x >> 16u) | (x << 16u);
#endif
}

uint CountLeadingZeros(uint x) {
    if(x == 0u) return 32u;
#if NativeGL
    return 31u - uint(findMSB(x));
#else
    uint n = 0u;
    if((x & 0xffff0000u) == 0u) { n += 16u; x <<= 16u; }
    if((x & 0xff000000u) == 0u) { n +=  8u; x <<=  8u; }
    if((x & 0xf0000000u) == 0u) { n +=  4u; x <<=  4u; }
    if((x & 0xc0000000u) == 0u) { n +=  2u; x <<=  2u; }
    if((x & 0x80000000u) == 0u) { n +=  1u; }
    return n;
#endif
}

#if NativeGL != 1
int findLSB(uint x) {
    if(x == 0u) return -1;
    uint v = x & (0u - x);
    int  n = 0;
    if((v & 0xFFFF0000u) != 0u) n += 16;
    if((v & 0xFF00FF00u) != 0u) n += 8;
    if((v & 0xF0F0F0F0u) != 0u) n += 4;
    if((v & 0xCCCCCCCCu) != 0u) n += 2;
    if((v & 0xAAAAAAAAu) != 0u) n += 1;
    return n;
}
#endif

uint MortonIndex(uvec2 p) {
    uint n = 8u;
    p = (p ^ (p << n)) & 0x00ff00ffu; n >>= 1u;
    p = (p ^ (p << n)) & 0x0f0f0f0fu; n >>= 1u;
    p = (p ^ (p << n)) & 0x33333333u; n >>= 1u;
    p = (p ^ (p << n)) & 0x55555555u; n >>= 1u;
    return (p.x << 1u) | p.y;
}

// from: https://psychopath.io/post/2021_01_30_building_a_better_lk_hash
uint OwenHashBase2(uint x, uint seed) { // seed is any random number
    x ^= x * 0x3d20adeau;
    x += seed;
    x *= (seed >> 16u) | 1u;
    x ^= x * 0x05526c56u;
    x ^= x * 0x53a22864u;
    return x;
}

uint OwenHashBase4(uint x, uint seed) {
    x ^= x * 0x3d20adeau;
    x ^= (x >> 1u) & (x << 1u) & 0x55555555u;
    x += seed;
    x *= (seed >> 16u) | 1u;
    x ^= (x >> 1u) & (x << 1u) & 0x55555555u;
    x ^= x * 0x05526c56u;
    x ^= x * 0x53a22864u;
    return x;
}

uint OwenScrambleBase2(uint p, uint seed) {
    return ReverseBits(OwenHashBase2(ReverseBits(p), seed));
}

uint OwenScrambleBase4(uint p, uint seed) {
    return ReverseBits(OwenHashBase4(ReverseBits(p), seed));
}

#define urot(x, k) (((x) << (k)) | ((x) >> (32u - (k))))

#define final(a, b, c) \
    { \
        c ^= b; \
        c -= urot(b, 14u); \
        a ^= c; \
        a -= urot(c, 11u); \
        b ^= a; \
        b -= urot(a, 25u); \
        c ^= b; \
        c -= urot(b, 16u); \
        a ^= c; \
        a -= urot(c, 4u); \
        b ^= a; \
        b -= urot(a, 14u); \
        c ^= b; \
        c -= urot(b, 24u); \
    } \

uint HashUint(uint kx) {
    uint a, b, c;
    a = b = c = 0xdeadbeefu + (1u << 2u) + 13u;
    
    a += kx;
    final(a, b, c);
    
    return c;
}

uint HashHpUint(uint i) {
    i ^= i >> 16u;
    i *= 0x21f0aaadu;
    i ^= i >> 15u;
    i *= 0xd35a2d97u;
    i ^= i >> 15u;
    return i ^ 0xe6fe3bebu;
}

// Computes direction number v_k for a given dimension and bit position
uint computeDirectionNumber(uint k, uint dimension) {
    if(dimension == 0u) {
        // Dimension 0: Placeholder; handled separately as bit-reverse
        return 0u;
    } else if(dimension == 1u) {
        // Polynomial x + 1
        uint v = 1u;
        for(uint i = 0u; i < k; i++) {
            v = (v << 1u) ^ v;  // v_i = v_{i-1} << 1 XOR v_{i-1}
        }
        return v;
    } else if(dimension == 2u) {
        // Polynomial x^2 + x + 1
        uvec2 v = uvec2(1u, 2u);
        if(k < 2u) return v[k];
        for(uint i = 2u; i <= k; i++) {
            v = uvec2(v[1], (v[0] << 2u) ^ v[1] ^ v[0]);
        }
        return v[1];
    } else if(dimension == 3u) {
        // Polynomial x^3 + x + 1
        uvec3 v = uvec3(1u, 2u, 3u);
        if(k < 3u) return v[k];
        for(uint i = 3u; i <= k; i++) {
            v = uvec3(v[1], v[2], (v.x << 3u) ^ v[2] ^ v[0]);
        }
        return v[2];
    }
    return 0u;  // Default case
}

const uvec4 SobolBurleyTable[] = uvec4[](
    uvec4(0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u), uvec4(0x00000002u, 0x00000003u, 0x00000003u, 0x00000003u), uvec4(0x00000004u, 0x00000005u, 0x00000006u, 0x00000004u), uvec4(0x00000008u, 0x0000000fu, 0x00000009u, 0x0000000au),
    uvec4(0x00000010u, 0x00000011u, 0x00000017u, 0x0000001fu), uvec4(0x00000020u, 0x00000033u, 0x0000003au, 0x0000002eu), uvec4(0x00000040u, 0x00000055u, 0x00000071u, 0x00000045u), uvec4(0x00000080u, 0x000000ffu, 0x000000a3u, 0x000000c9u),
    uvec4(0x00000100u, 0x00000101u, 0x00000116u, 0x0000011bu), uvec4(0x00000200u, 0x00000303u, 0x00000339u, 0x000002a4u), uvec4(0x00000400u, 0x00000505u, 0x00000677u, 0x0000079au), uvec4(0x00000800u, 0x00000f0fu, 0x000009aau, 0x00000b67u),
    uvec4(0x00001000u, 0x00001111u, 0x00001601u, 0x0000101eu), uvec4(0x00002000u, 0x00003333u, 0x00003903u, 0x0000302du), uvec4(0x00004000u, 0x00005555u, 0x00007706u, 0x00004041u), uvec4(0x00008000u, 0x0000ffffu, 0x0000aa09u, 0x0000a0c3u),
    uvec4(0x00010000u, 0x00010001u, 0x00010117u, 0x0001f104u), uvec4(0x00020000u, 0x00030003u, 0x0003033au, 0x0002e28au), uvec4(0x00040000u, 0x00050005u, 0x00060671u, 0x000457dfu), uvec4(0x00080000u, 0x000f000fu, 0x000909a3u, 0x000c9baeu),
    uvec4(0x00100000u, 0x00110011u, 0x00171616u, 0x0011a105u), uvec4(0x00200000u, 0x00330033u, 0x003a3939u, 0x002a7289u), uvec4(0x00400000u, 0x00550055u, 0x00717777u, 0x0079e7dbu), uvec4(0x00800000u, 0x00ff00ffu, 0x00a3aaaau, 0x00b6dba4u),
    uvec4(0x01000000u, 0x01010101u, 0x01170001u, 0x0100011au), uvec4(0x02000000u, 0x03030303u, 0x033a0003u, 0x030002a7u), uvec4(0x04000000u, 0x05050505u, 0x06710006u, 0x0400079eu), uvec4(0x08000000u, 0x0f0f0f0fu, 0x09a30009u, 0x0a000b6du),
    uvec4(0x10000000u, 0x11111111u, 0x16160017u, 0x1f001001u), uvec4(0x20000000u, 0x33333333u, 0x3939003au, 0x2e003003u), uvec4(0x40000000u, 0x55555555u, 0x77770071u, 0x45004004u), uvec4(0x80000000u, 0xffffffffu, 0xaaaa00a3u, 0xc900a00au)
);

float SobolBurley(uint rev_bit_index, const uint dimension, const uint scramble_seed) {
    uint result = 0u;
    
    if(dimension == 0u) {
        result = ReverseBits(rev_bit_index);
    } else {
        uint fwd = ReverseBits(rev_bit_index);
        while (fwd != 0u) {
            uint bit = uint(findLSB(fwd));
            result ^= SobolBurleyTable[bit][dimension];
            fwd &= fwd - 1u;
        }
    }
    
    result = ReverseBits(OwenHashBase2(result, scramble_seed));
    
    return float(result) * (1.0 / 4294967296.0);
}

float SobolBurley1D(uint index, const uint dimension, uint seed, const uint shuffled_index_mask) {
    seed ^= HashHpUint(dimension);
    index = OwenHashBase2(ReverseBits(index), seed ^ 0xbff95bfeu);
    index &= shuffled_index_mask;
    return SobolBurley(index, 0u, seed ^ 0x635c77bdu);
}

vec2 SobolBurley2D(uint index, const uint dimension, uint seed, const uint shuffled_index_mask) {
    seed ^= HashHpUint(dimension);
    index = OwenHashBase2(ReverseBits(index), seed ^ 0xf8ade99au);
    index &= shuffled_index_mask;
    return vec2(SobolBurley(index, 0u, seed ^ 0xe0aaaf76u),
                SobolBurley(index, 1u, seed ^ 0x94964d4eu));
}

vec3 SobolBurley3D(uint index, const uint dimension, uint seed, const uint shuffled_index_mask) {
    seed ^= HashHpUint(dimension);
    index = OwenHashBase2(ReverseBits(index), seed ^ 0xcaa726acu);
    index &= shuffled_index_mask;
    return vec3(SobolBurley(index, 0u, seed ^ 0x9e78e391u),
                SobolBurley(index, 1u, seed ^ 0x67c33241u),
                SobolBurley(index, 2u, seed ^ 0x78c395c5u));
}

vec4 SobolBurley4D(uint index, const uint dimension, uint seed, const uint shuffled_index_mask) {
    seed ^= HashHpUint(dimension);
    index = OwenHashBase2(ReverseBits(index), seed ^ 0xc2c1a055u);
    index &= shuffled_index_mask;
    return vec4(SobolBurley(index, 0u, seed ^ 0x39468210u),
                SobolBurley(index, 1u, seed ^ 0xe9d8a845u),
                SobolBurley(index, 2u, seed ^ 0x5f32b482u),
                SobolBurley(index, 3u, seed ^ 0x1524cc56u));
}

uvec3 blueNoiseIndexing(uint px, uint sp) {
    px = OwenScrambleBase4(px, HashUint(sp)); // Scramble px off "Samples to avoid bias"
    return uvec3(sp * 2u + 1u + px, 0u, 0xffffffffu);
}

float hash1(hash_seed kernel, uint dim) {
    uvec3 idx = blueNoiseIndexing(kernel.pixelrng, kernel.samples);
    return SobolBurley1D(idx.x, dim, idx.y, idx.z);
}

vec2 hash2(hash_seed kernel, uint dim) {
    uvec3 idx = blueNoiseIndexing(kernel.pixelrng, kernel.samples);
    return SobolBurley2D(idx.x, dim, idx.y, idx.z);
}

vec3 hash3(hash_seed kernel, uint dim) {
    uvec3 idx = blueNoiseIndexing(kernel.pixelrng, kernel.samples);
    return SobolBurley3D(idx.x, dim, idx.y, idx.z);
}

vec4 hash4(hash_seed kernel, uint dim) {
    uvec3 idx = blueNoiseIndexing(kernel.pixelrng, kernel.samples);
    return SobolBurley4D(idx.x, dim, idx.y, idx.z);
}

uint initHashSeed(uvec2 px) {
    // Blue Noise scrambling
    return OwenScrambleBase4(MortonIndex(px), HashUint(seed));
}