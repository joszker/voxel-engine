#include "VoxelRenderer.h"

#include "Base/FileSystem.h"

#include <GLFW/glfw3.h>


// sparse voxel tree
struct SVT
{
    u32 VoxelCount;
    u32 Depth;                          
    VkDeviceAddress NodeArrayPtr;      // SVNode*
    VkDeviceAddress VoxelArrayPtr;     // Voxel* 
};

// voxel hash map
struct VHM
{
    VkDeviceAddress KeysPtr;
    VkDeviceAddress ValuesPtr;
};

struct PushConstants
{
    Vec3 AmbientLight;
    f32 AmbientFalloff;
    Vec3 SunDir;
    f32 SunConeThetaCos;
    Vec3 SunLight;
    float AmbientMinimum;
    Vec4 SkyColor;

    Vec4 CameraPos;
    Vec3 CameraDir;
    f32 CameraFOV;
    Vec2 Resolution;
    u32 FrameNo;
    u32 Debug;
    u32 FramesPerSecond;

    SVT SparseVoxels;
    VHM CurrentVHM;
    VHM NextVHM;
};
static_assert(sizeof(PushConstants) <= 256);

/* Resource bindings */
#define BIND_IMAGE_VOXEL        0
#define BIND_IMAGE_NORMAL       1
#define BIND_IMAGE_STBN         2
#define BIND_IMAGE_SPECULAR     3
#define BIND_BUFFER_PALETTE     5

/* Voxel hash map */
constexpr size_t VHM_SIZE = 33554432;//16777216;
constexpr size_t VHM_KEY_SIZE = 4;
constexpr size_t VHM_ENTRY_SIZE = 32;
constexpr size_t VHM_BUFFER_SIZE = (VHM_SIZE * (VHM_KEY_SIZE + VHM_ENTRY_SIZE));

/* How often the hashmap is refreshed */
constexpr f64 PURGE_PERIOD = 1.0;


void VoxelRenderer::Initialize(RenderDevice* device, SwapChain *swapChain, bool enableSpecular)
{
    m_device = device;
    m_swapChain = swapChain;
    m_enableSpecular = enableSpecular;
    m_cameraPos = Vec3(0);
    m_cameraDir = Vec3(0, 1, 0);
    m_cameraFOV = glm::radians(90.0f);

    /* Create image sampler */
    {
        VkSamplerCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 0.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = {},
            .unnormalizedCoordinates = VK_FALSE,
        };
        vkCreateSampler(*m_device, &info, nullptr, &m_imageSampler);
    }


    /* Create descriptor layout */
    {
        DescriptorLayoutBuilder layoutBuilder;
        layoutBuilder.Add(BIND_IMAGE_VOXEL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(BIND_IMAGE_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(BIND_IMAGE_STBN, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(BIND_IMAGE_SPECULAR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(BIND_BUFFER_PALETTE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        m_layout = layoutBuilder.Build(*m_device, VK_SHADER_STAGE_ALL);
    }


    /* Allocate descriptor sets for each frame in flight */
    {
        DescriptorAllocator descriptorAlloc;

        m_frames.resize(m_swapChain->GetFrameCount());
        for (Frame& frame : m_frames)
        {
            descriptorAlloc.Add(&m_layout, &frame.descriptorSet);
        }

        m_descriptorPool = descriptorAlloc.Allocate(*m_device);
    }


    /* Create images for each frame in flight */
    {
        createImages(m_swapChain->GetSize().width, m_swapChain->GetSize().height);
    }


    /* Create pipelies */
    {
        VkShaderModule fullscreenShader = loadShader("data/shaders/fullscreen.vert.spv");
        VkShaderModule primaryShader = loadShader(
            m_enableSpecular ? "data/shaders/primary-specular.frag.spv" : "data/shaders/primary.frag.spv"
        );
        VkShaderModule compositeShader = loadShader(
            m_enableSpecular ? "data/shaders/composite-specular.frag.spv" : "data/shaders/composite.frag.spv"
        );
        VkShaderModule decayShader = loadShader("data/shaders/decay.comp.spv");
        VkShaderModule purgeShader = loadShader("data/shaders/purge.comp.spv");
        
        // Create primary pipeline
        {
            PipelineBuilder pb;

            pb.AddVertexShader(fullscreenShader);
            pb.AddFragmentShader(primaryShader);
            pb.AddColorAttachment({ m_frames[0].voxelImage.GetFormat() });
            pb.AddColorAttachment({ m_frames[0].normalImage.GetFormat() });
            pb.AddColorAttachment({ m_frames[0].specularImage.GetFormat() });

            pb.AddDescriptorLayout(m_layout);
            pb.AddPushConstantRange({
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                .size = sizeof(PushConstants),
            });

            m_primaryPass = pb.Build(*m_device);
        }

        // Create composite pipeline
        {
            PipelineBuilder pb;

            pb.AddVertexShader(fullscreenShader);
            pb.AddFragmentShader(compositeShader);
            pb.AddColorAttachment({ m_swapChain->GetImageFormat() });
            pb.AddDescriptorLayout(m_layout);
            pb.AddPushConstantRange({
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                .size = sizeof(PushConstants),
            });

            m_compositePass = pb.Build(*m_device);
        }

        // Create compute pipelines
        {
            std::array pushConstantRanges = {
                VkPushConstantRange {
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                    .size = sizeof(PushConstants),
                },
            };

            m_decayPipe = CreateComputePipeline(*m_device, {
                .ShaderModule = decayShader,
                .PushConstantRanges = pushConstantRanges,
            });

            m_purgePipe = CreateComputePipeline(*m_device, {
                .ShaderModule = purgeShader,
                .PushConstantRanges = pushConstantRanges,
            });
        }

        vkDestroyShaderModule(*m_device, fullscreenShader, nullptr);
        vkDestroyShaderModule(*m_device, primaryShader, nullptr);
        vkDestroyShaderModule(*m_device, compositeShader, nullptr);
        vkDestroyShaderModule(*m_device, decayShader, nullptr);
        vkDestroyShaderModule(*m_device, purgeShader, nullptr);
    }


    /* Create VHM buffer */
    {
        m_vhmBuffer = m_device->CreateBuffer({
            .Size = 2 * VHM_BUFFER_SIZE,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });

        m_vhmCurrentPtr = 0;
        m_vhmNextPtr = m_vhmCurrentPtr + VHM_BUFFER_SIZE;
    }


    /* Create palette buffer */
    {
        m_paletteBuffer = m_device->CreateBuffer({
            .Size = sizeof(VoxelMaterial) * 256,
            .Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .AllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });
    }

    m_nextPurgeTime = glfwGetTime() + 2.0;
}


void VoxelRenderer::Shutdown()
{

}


void VoxelRenderer::SetCamera(Vec3 cameraPosition, Vec3 cameraDirection, float cameraFOV)
{
    m_cameraPos = cameraPosition;
    m_cameraDir = cameraDirection;
    m_cameraFOV = cameraFOV;
}


void VoxelRenderer::SetParams(const VoxelRenderParams& params)
{
    m_params = params;
    m_decayVHM = true;
}


void VoxelRenderer::SetFinalRenderProc(std::function<void(CommandBuffer&)> proc)
{
    m_finalRenderProc = proc;
}


void VoxelRenderer::UpdateSparseVoxelTree(const SVTree& svt)
{
    auto srcNodes = svt.GetNodes();
    auto srcVoxels = svt.GetVoxels();

    const size_t nodeDstOffset = 0;
    const size_t nodeBytes = sizeof(SVNode) * srcNodes.size();

    const size_t voxelDstOffset = nodeDstOffset + nodeBytes;
    const size_t voxelBytes = sizeof(Voxel) * srcVoxels.size(); 

    const size_t svtBytes = nodeBytes + voxelBytes;

    if (svtBytes > m_svtBuffer.size) // The current buffer are to small:
    {
        // Delete previous buffers:
        if (m_svtBuffer.gpu.GetAllocation())
            m_device->DestroyBuffer(m_svtBuffer.gpu);

        if (m_svtBuffer.cpu.GetAllocation())
            m_device->DestroyBuffer(m_svtBuffer.cpu);

        m_svtBuffer.size =
            sizeof(SVNode) * (srcNodes.size() + 32768) + 
            sizeof(Voxel) * (srcVoxels.size() + 131072);

        // Create new buffers:
        m_svtBuffer.gpu = m_device->CreateBuffer({
            .Size = m_svtBuffer.size,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        });

        m_svtBuffer.cpu = m_device->CreateBuffer({
            .Size = m_svtBuffer.size,
            .Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .AllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        });        
    }

    m_device->Write(&m_svtBuffer.cpu, nodeDstOffset, srcNodes.data(), nodeBytes);
    m_device->Write(&m_svtBuffer.cpu, voxelDstOffset, srcVoxels.data(), voxelBytes);
    
    m_svtBuffer.voxelArrayPtr = voxelDstOffset;
    m_svtBuffer.voxelCount = srcVoxels.size();
    m_svtBuffer.depth = svt.GetDepth();
    m_svtBuffer.modified = true;
    m_decayVHM = true;
}


void VoxelRenderer::UpdatePalette(const VoxelPalette& palette)
{
    m_device->UploadBuffer(&m_paletteBuffer, 0, palette.data(), palette.size() * sizeof(VoxelMaterial));
    m_decayVHM = true;
}


void VoxelRenderer::Render(Image& dstImage, CommandBuffer& cmdBuffer)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];

    /* Write push constants */
    PushConstants pc;
    {
        pc.AmbientLight = m_params.AmbientLight;
        pc.AmbientFalloff = 1.0f / m_params.AmbientFalloff;
        pc.AmbientMinimum = m_params.AmbientMinimum;
        pc.SunLight = m_params.SunLight;
        pc.SunDir.x = cos(m_params.SunElevationAngle) * sin(m_params.SunAzimuthAngle);
        pc.SunDir.y = cos(m_params.SunElevationAngle) * cos(m_params.SunAzimuthAngle);
        pc.SunDir.z = sin(m_params.SunElevationAngle);
        pc.SunDir = glm::normalize(pc.SunDir);
        pc.SunConeThetaCos = cos(m_params.SunConeAngle);
        pc.CameraPos = Vec4(m_cameraPos, 0.0f);
        pc.CameraDir = Vec3(m_cameraDir);
        pc.CameraFOV = m_cameraFOV;
        pc.SkyColor = Vec4(m_params.SkyColor, 1.0f);

        
        auto swapChainSize = m_swapChain->GetSize();
        pc.Resolution = Vec2(swapChainSize.width, swapChainSize.height);
        pc.FrameNo = m_frameId;
        pc.Debug = m_debug;

        pc.SparseVoxels.NodeArrayPtr = m_svtBuffer.gpu.GetDeviceAddress();
        pc.SparseVoxels.VoxelArrayPtr = pc.SparseVoxels.NodeArrayPtr + m_svtBuffer.voxelArrayPtr;
        pc.SparseVoxels.Depth = m_svtBuffer.depth;
        pc.SparseVoxels.VoxelCount = m_svtBuffer.voxelCount;

        pc.CurrentVHM.KeysPtr = m_vhmBuffer.GetDeviceAddress() + m_vhmCurrentPtr;
        pc.CurrentVHM.ValuesPtr = pc.CurrentVHM.KeysPtr + VHM_KEY_SIZE * VHM_SIZE;

        pc.NextVHM.KeysPtr = m_vhmBuffer.GetDeviceAddress() + m_vhmNextPtr;
        pc.NextVHM.ValuesPtr = pc.NextVHM.KeysPtr + VHM_KEY_SIZE * VHM_SIZE;        
    }

    /* Write descriptors */
    {
        DescriptorWriter writer;

        writer.BindSet(frame.descriptorSet);
        {;
            writer.Write(BIND_IMAGE_VOXEL, frame.voxelImage, m_imageSampler);
            writer.Write(BIND_IMAGE_NORMAL, frame.normalImage, m_imageSampler);
            writer.Write(BIND_IMAGE_SPECULAR, frame.specularImage, m_imageSampler);
            writer.Write(BIND_BUFFER_PALETTE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_paletteBuffer);
        }
        writer.Update(*m_device);
    }

    if (m_svtBuffer.modified)
    {
        m_swapChain->WaitForPreviousFrames();   // make sure no frames are in flight

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,          
        }, &m_svtBuffer.gpu);

        // Copy: cpu buffer ---> gpu buffer
        cmdBuffer.Copy(&m_svtBuffer.cpu, &m_svtBuffer.gpu);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
        }, &m_svtBuffer.gpu);
    }


    if (m_frameId == 0 || m_clearVHM)
    {
        // Clear voxel hashmap:
        m_clearVHM = false;

        cmdBuffer.Fill(&m_vhmBuffer, m_vhmCurrentPtr, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_vhmCurrentPtr + VHM_SIZE * 4, VHM_SIZE * VHM_ENTRY_SIZE, 0);
        cmdBuffer.Fill(&m_vhmBuffer, m_vhmNextPtr, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_vhmNextPtr + VHM_SIZE * 4, VHM_SIZE * VHM_ENTRY_SIZE, 0);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
        }, &m_vhmBuffer);

        m_nextPurgeTime = glfwGetTime() + PURGE_PERIOD;
    }


    //
    {
        cmdBuffer.Barrier({
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            
        }, &m_vhmBuffer); 
    }

    /* Primary pass */
    {
        RenderPass renderPass;
        renderPass.Area = frame.voxelImage.GetArea(),
        renderPass.Attachments = { 
            {
                .Image = &frame.voxelImage,
                .ClearValue = { VkClearColorValue{.uint32 = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX }} },
            },
            {
                .Image = &frame.normalImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },
            {
                .Image = &frame.specularImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },      
        };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_primaryPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        cmdBuffer.EndRenderPass();

        // Transition images:
        {
            MemoryBarrier barrier = {
                .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .NextAccess = VK_ACCESS_SHADER_READ_BIT,
                .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            };

            cmdBuffer.Barrier(barrier, &frame.voxelImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            cmdBuffer.Barrier(barrier, &frame.normalImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            cmdBuffer.Barrier(barrier, &frame.specularImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    if (m_decayVHM)
    {
        // Decay accumulated vhm values:
        m_decayVHM = false;

        m_swapChain->WaitForPreviousFrames();

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        }, &m_vhmBuffer);

        /* Compute pass */
        vkCmdPushConstants(cmdBuffer, m_decayPipe.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_decayPipe);

        vkCmdDispatch(cmdBuffer, VHM_SIZE / 1024, 1, 1);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        }, &m_vhmBuffer);
    }


    f64 currentTime = glfwGetTime();
    if (currentTime > m_nextPurgeTime)
    {
        pc.FramesPerSecond = u32(std::round(f64(m_framesSincePurge) / PURGE_PERIOD));

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        }, &m_vhmBuffer);

        cmdBuffer.Fill(&m_vhmBuffer, m_vhmNextPtr, VHM_SIZE * VHM_KEY_SIZE, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_vhmNextPtr + VHM_SIZE * VHM_KEY_SIZE, VHM_SIZE * VHM_ENTRY_SIZE, 0);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        }, &m_vhmBuffer);

        vkCmdPushConstants(cmdBuffer, m_purgePipe.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_purgePipe);
        vkCmdDispatch(cmdBuffer, VHM_SIZE / 1024, 1, 1); 
        
        std::swap(m_vhmCurrentPtr, m_vhmNextPtr);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        }, &m_vhmBuffer);

        m_nextPurgeTime = currentTime + PURGE_PERIOD;
        m_framesSincePurge = 0;
    }


    /* Composite pass */
    {
        Attachment mainAttachment = {
            .Image = &dstImage,
            .ClearValue = { VkClearColorValue{ m_params.SkyColor.r, m_params.SkyColor.g, m_params.SkyColor.b, 1.0f } },
        };

        RenderPass renderPass;
        renderPass.Area = m_swapChain->GetArea();
        renderPass.Attachments = { mainAttachment };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_compositePass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        if (m_finalRenderProc)
            m_finalRenderProc(cmdBuffer);

        cmdBuffer.EndRenderPass();
    }

    m_svtBuffer.modified = false;
    m_frameId += 1;
    m_framesSincePurge += 1;
}


void VoxelRenderer::Resize(int width, int height)
{
    createImages(width, height);
}


void VoxelRenderer::ClearVoxelCache()
{
    m_clearVHM = true;
}


void VoxelRenderer::SetDebug(int debug)
{
    m_debug = debug;
}


void VoxelRenderer::createImages(int width, int height)
{
    if (width == 0 || height == 0)
        return;

    for (Frame& frame : m_frames)
    {
        frame.voxelImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_R32G32B32A32_UINT,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });

        frame.normalImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_B8G8R8A8_UNORM,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });

        frame.specularImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });        
    }
}


VkShaderModule VoxelRenderer::loadShader(std::string_view path)
{
    auto code = FileSystem::ReadFile(path);
    if (!code)
    {
        LOG_ERROR("Failed to load shader: '{}'", path);
        return VK_NULL_HANDLE;
    }

    return m_device->CreateShaderModule(*code);
}