#pragma once
#include "Voxel/SVTree.h"

#include "Renderer/RenderDevice.h"
#include "Renderer/SwapChain.h"
#include "Renderer/DescriptorSet.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"


struct alignas(16) VoxelMaterial
{
    Vec4u8 color{};         // base color
    float emission{};       // strength of emitted light
    float specular{};       // percentage of specular rays, the rest is diffuse
};

using VoxelPalette = std::array<VoxelMaterial, 256>;


struct VoxelRenderParams
{
    Vec3 AmbientLight;
    float AmbientFalloff;
    float AmbientMinimum;
    Vec3 SunLight;
    Vec3 SkyColor;
    float SunElevationAngle;    // Sun elevation in radians (0.5pi == zenith)
    float SunAzimuthAngle;      // Sun azimuth in radians
    float SunConeAngle;         // Sun cone angle in radians
};


/* 
    Voxel renderer. Renders a sparse voxel tree.
*/
class VoxelRenderer
{
public:
    VoxelRenderer() = default;

    void Initialize(RenderDevice* device, SwapChain *swapChain, bool enableSpecular);
    void Shutdown();

    void SetCamera(Vec3 cameraPosition, Vec3 cameraDirection, float cameraFOV);
    void SetParams(const VoxelRenderParams& params);
    void SetFinalRenderProc(std::function<void(CommandBuffer&)> proc);

    void UpdateSparseVoxelTree(const SVTree& svt);
    void UpdatePalette(const VoxelPalette& palette);

    void Render(Image& swapImage, CommandBuffer& cmdBuffer);

    void Resize(int width, int height);
    void ClearVoxelCache();
    void SetDebug(int debug);

private:
    void createImages(int width, int height);
    VkShaderModule loadShader(std::string_view path);

private:
    RenderDevice *m_device{};
    SwapChain *m_swapChain{};

    VkDescriptorPool m_descriptorPool{};
    DescriptorLayout m_layout{};        // Layout used for all pipelines
    Pipeline m_primaryPass{};
    Pipeline m_compositePass{};
    Pipeline m_decayPipe{};
    Pipeline m_purgePipe{};

    VkSampler m_imageSampler;

    struct
    {
        bool modified = false;
        size_t size = 0;
        size_t voxelArrayPtr = 0;
        size_t voxelCount = 0;
        u32 depth = 0;

        Buffer gpu{};     // gpu local buffer
        Buffer cpu{};     // cpu staging buffer
    } m_svtBuffer;

    Buffer m_vhmBuffer{};
    size_t m_vhmCurrentPtr = 0;
    size_t m_vhmNextPtr = 0;

    Buffer m_paletteBuffer{};
   
    struct Frame
    {
        VkDescriptorSet descriptorSet{};

        Image voxelImage;
        Image normalImage;
        Image specularImage;
    };
    std::vector<Frame> m_frames;

    u32 m_frameId = 0;
    u32 m_debug = 0;
    f64 m_nextPurgeTime = 0.0;
    u32 m_framesSincePurge = 0;

    Vec3 m_cameraPos{};
    Vec3 m_cameraDir{};
    float m_cameraFOV{};
    VoxelRenderParams m_params{};
    std::function<void(CommandBuffer&)> m_finalRenderProc;

    bool m_enableSpecular = false;
    bool m_clearVHM = false;
    bool m_decayVHM = false;
};





