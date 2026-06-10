#include "MainScene.h"
#include "Application.h"
#include "Base/Log.h"
#include "Base/FileSystem.h"
#include "Renderer/DescriptorSet.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/bit.hpp>

#include "Voxel/SVTree.h"

#define OGT_VOX_IMPLEMENTATION
#include "3rdparty/ogt_vox.h"

#define STB_IMAGE_IMPLEMENTATION
#include "3rdparty/stb_image.h"

struct GPU_SVTree
{
    u32 root;
    u32 depth;
    VkDeviceAddress nodes;      // SVNode*
    VkDeviceAddress voxels;     // Voxel* 
};

#define VHM_SIZE            16777216
#define VHM_BUFFER_SIZE     (VHM_SIZE * 20)

struct GPU_VoxelHashMap
{
    VkDeviceAddress keys;
    VkDeviceAddress values;
};

struct PushConstants
{
    Vec4 CameraPos;
    Vec4 CameraDir;
    Vec2 Resolution;
    u32 FrameNo;

    GPU_SVTree VoxelMap;
    GPU_VoxelHashMap CurrentVHM;
    GPU_VoxelHashMap PrevVHM;
};






void MainScene::OnAttach()
{
    auto voxData = FileSystem::ReadFile("data/room.vox");
    if (voxData)
    {
        auto *scene = ogt_vox_read_scene(voxData->data(), voxData->size());

#if 1
        Vec3i boundsMin(INT32_MAX), boundsMax(INT32_MIN);

        for (uint32_t i = 0; i < scene->num_instances; i++)
        {
            auto *instance = scene->instances + i;
            auto *model = scene->models[instance->model_index];

            Vec3i position = Vec3(instance->transform.m30, instance->transform.m31, instance->transform.m32);
            Vec3i size = Vec3(model->size_x, model->size_y, model->size_z);

            boundsMin = glm::min(boundsMin, Vec3i(glm::floor(Vec3(position) - Vec3(size) / 2.0f)));
            boundsMax = glm::max(boundsMax, Vec3i(glm::ceil(Vec3(position) + Vec3(size) / 2.0f)));
        }

        Vec3u sceneSize = boundsMax - boundsMin;
        VoxelArray voxels{};
        voxels.Init(sceneSize);

        for (uint32_t i = 0; i < scene->num_instances; i++)
        {
            auto *instance = scene->instances + i;
            auto *model = scene->models[instance->model_index];

            Vec3i size = Vec3(model->size_x, model->size_y, model->size_z);
            Vec3i position = Vec3(instance->transform.m30, instance->transform.m31, instance->transform.m32) - Vec3(size / 2);
            
            for (u32 z = 0; z < size.z; z++)
            {
                for (u32 y = 0; y < size.y; y++)
                {
                    const uint8_t *srcVoxels = &model->voxel_data[z * (size.x * size.y) + y * size.x];

                    Vec3u pos = (position + Vec3i(0, y, z)) - boundsMin;
                    
                    memcpy(&voxels.Data[pos.z * (voxels.Size.x * voxels.Size.y) + pos.y * voxels.Size.x + pos.x], srcVoxels, size.x * sizeof(Voxel));
                }
            }
        }
#else

        auto *model = scene->models[0];

        VoxelArray voxels{};
        voxels.Size = Vec3u(model->size_x, model->size_y, model->size_z);

        size_t size = model->size_x * model->size_y * model->size_z;

        voxels.Data.resize(size);
        memcpy(voxels.Data.data(), model->voxel_data, sizeof(Voxel) * size);
        //for (size_t i = 0; i < size; i++)
        //    voxels.Data.push_back(Voxel{ model->voxel_data[i] });
#endif

        m_svt = SVTree(voxels);
    }


    m_camera.pos = Vec3(0.0f, 0.0f, 0.0f);
    m_camera.yaw = glm::half_pi<f32>();
    m_camera.dir = {
        cos(m_camera.yaw) * cos(m_camera.pitch),
        sin(m_camera.yaw) * cos(m_camera.pitch),
        sin(m_camera.pitch),
    };
    m_camera.active = true;

    {
        double x, y;
        glfwGetCursorPos(m_app->GetWindow(), &x, &y);
        m_prevCursorPos = Vec2(x, y);
    }

    initImGui();

    /* Create sampler */
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

        vkCreateSampler(*m_device, &info, nullptr, &m_sampler);
    }

    /* Create descriptor layout */
    DescriptorLayout layout;
    {
        DescriptorLayoutBuilder layoutBuilder;
        //layoutBuilder.Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        layoutBuilder.Add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layout = layoutBuilder.Build(*m_device, VK_SHADER_STAGE_ALL);
    }


    {
        DescriptorAllocator descriptorAlloc;

        m_frames.resize(m_swapChain->GetFrameCount());
        for (Frame& frame : m_frames)
        {
            descriptorAlloc.Add(&layout, &frame.descriptorSet);
        }

        m_descriptorPool = descriptorAlloc.Allocate(*m_device);
    }
    createImages(m_swapChain->GetSize().width, m_swapChain->GetSize().height);

    /* Load shaders */
    VkShaderModule fullscreenShader = loadShader("data/shaders/fullscreen.vert.spv");
    VkShaderModule primaryShader = loadShader("data/shaders/primary.frag.spv");
    VkShaderModule compositeShader = loadShader("data/shaders/composite.frag.spv");
    VkShaderModule denoisingShader = loadShader("data/shaders/denoise.comp.spv");

    /* Create primary pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(primaryShader);
        //pipelineBuilder.AddColorAttachment({ m_frames[0].primaryImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].voxelImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].normalImage.GetFormat() });        

        pipelineBuilder.AddDescriptorLayout(layout);
        pipelineBuilder.AddPushConstantRange({
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(PushConstants),
        });

        m_primaryPass = pipelineBuilder.Build(*m_device);
    }

    /* Create composite pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(compositeShader);
        pipelineBuilder.AddColorAttachment({ m_swapChain->GetImageFormat() });
        pipelineBuilder.AddDescriptorLayout(layout);
        pipelineBuilder.AddPushConstantRange({
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(PushConstants),
        });

        m_compositePass = pipelineBuilder.Build(*m_device);
    }

    {
        std::array pcRanges = {
            VkPushConstantRange {
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                .size = sizeof(PushConstants),
            },
        };
        
        ComputePipelineDesc desc = {
            .ShaderModule = denoisingShader,
            .PushConstantRanges = pcRanges,
        };

        m_denoisingPass = CreateComputePipeline(*m_device, desc);
    }

    vkDestroyShaderModule(*m_device, fullscreenShader, nullptr);
    vkDestroyShaderModule(*m_device, primaryShader, nullptr);
    vkDestroyShaderModule(*m_device, compositeShader, nullptr);

    /* Storage buffers */
    {
        const size_t size = sizeof(SVNode) * m_svt.GetNodes().size() + sizeof(Voxel) * m_svt.GetVoxels().size();

        m_svtBuffer.gpu = m_device->CreateBuffer({
            .Size = size,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        });

        m_svtBuffer.cpu = m_device->CreateBuffer({
            .Size = size,
            .Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .AllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        });
    }

    {
        /*
        m_voxelFaceBuffer = m_device->CreateBuffer({
            .Size = 1920 * 1080 * 16,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });*/

        m_vhmBuffer = m_device->CreateBuffer({
            .Size = 2 * VHM_BUFFER_SIZE,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });
    }

    {
        int width, height, channels;
        u8 *stbnTexRGBA = stbi_load("data/stbn_vec2.png", &width, &height, &channels, 4);


        if (stbnTexRGBA)
        {
            std::vector<u8> stbnTexRG;
            stbnTexRG.resize(width * height * 2);

            for (size_t i = 0; i < width * height; i++)
            {
                stbnTexRG[2 * i + 0] = stbnTexRGBA[4 * i + 0];
                stbnTexRG[2 * i + 1] = stbnTexRGBA[4 * i + 1];
            }
            stbi_image_free(stbnTexRGBA);

            m_stbnImage = m_device->CreateImage({
                .Type = VK_IMAGE_TYPE_2D,
                .Format = VK_FORMAT_R8G8_UNORM,
                .Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .Size = { (u32)width, (u32)height, 1 },
                .MipLevels = 1,
                .ViewType = VK_IMAGE_VIEW_TYPE_2D,
            });

            m_device->UploadImage(&m_stbnImage, stbnTexRG.data(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    m_prevFpsTime = glfwGetTime();
    m_fps = 0;

    m_currentVHM = 0;
    m_prevVHM = m_currentVHM + VHM_BUFFER_SIZE;
}


void MainScene::OnDetach()
{
    deinitImGui();
}


void MainScene::OnUpdate(f32 dt)
{
    f64 currentTime = glfwGetTime();

    if (currentTime - m_prevFpsTime >= 1.0)
    {
        m_fps = m_frameCounter;
        m_frameCounter = 0;
        m_prevFpsTime = currentTime;
    }
    m_frameCounter += 1;

    const Vec3 upAxis = Vec3(0.0f, 0.0f, 1.0f);

    const Vec3 cameraRight = -glm::normalize(glm::cross(upAxis, m_camera.dir));
    const Vec3 cameraUp = -glm::cross(m_camera.dir, cameraRight);


    if (m_camera.active)
    {
        float forward = 0.0f;
        float right = 0.0f;
        float upward = 0.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_W))
            forward = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_A))
            right = -1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_S))
            forward = -1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_D))
            right = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_SPACE))
            upward = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_LEFT_SHIFT))
            upward = -1.0f;

        m_camera.pos += 40.0f * (forward * m_camera.dir + right * cameraRight + upward * upAxis) * dt;
    }


    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
        auto imViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(imViewport->WorkPos, ImGuiCond_Always);

        auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::Begin("overlay", 0, flags);

        ImGui::Text("FPS: %u", m_fps);
        ImGui::Separator();

        ImGui::Text("X: %.3f", m_camera.pos.x);
        ImGui::Text("Y: %.3f", m_camera.pos.y);
        ImGui::Text("Z: %.3f", m_camera.pos.z);

        ImGui::End();
    }


    ImGui::Render();
}


void MainScene::OnRender(Image& dstImage, CommandBuffer& cmdBuffer)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];

    std::swap(m_currentVHM, m_prevVHM);

    PushConstants pc;
    {
        pc.CameraPos = Vec4(m_camera.pos, 0.0f);
        pc.CameraDir = Vec4(m_camera.dir, 0.0f);

        int width, height;
        glfwGetFramebufferSize(m_app->GetWindow(), &width, &height);
        pc.Resolution = Vec2(width, height);
        pc.FrameNo = m_frameNum++;

        pc.VoxelMap.nodes = m_svtBuffer.gpu.GetDeviceAddress();
        pc.VoxelMap.voxels = pc.VoxelMap.nodes + sizeof(SVNode) * m_svt.GetNodes().size();
        pc.VoxelMap.depth = m_svt.GetDepth();


        pc.CurrentVHM.keys = m_vhmBuffer.GetDeviceAddress() + m_currentVHM;
        pc.CurrentVHM.values = pc.CurrentVHM.keys + sizeof(u32) * VHM_SIZE;

        pc.PrevVHM.keys = m_vhmBuffer.GetDeviceAddress() + m_prevVHM;
        pc.PrevVHM.values = pc.PrevVHM.keys + sizeof(u32) * VHM_SIZE;
    }

    if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_T) == GLFW_PRESS)
    {
        m_swapChain->WaitForPreviousFrames();
      
        const size_t k_dataSize = sizeof(SVNode) * m_svt.GetNodes().size() + sizeof(Voxel) * m_svt.GetVoxels().size();
        static byte *data = nullptr;
        if (!data) data = new byte[k_dataSize];

        {
            auto srcNodes = m_svt.GetNodes();
            auto srcVoxels = m_svt.GetVoxels();

            SVNode *dstNodes = (SVNode*)data;
            Voxel *dstVoxels = (Voxel*)(dstNodes + srcNodes.size());

            memcpy(dstNodes, srcNodes.data(), sizeof(SVNode) * srcNodes.size());
            memcpy(dstVoxels, srcVoxels.data(), sizeof(Voxel) * srcVoxels.size());

            m_device->Write(&m_svtBuffer.cpu, 0, data, k_dataSize);
        }

        cmdBuffer.Copy(&m_svtBuffer.cpu, &m_svtBuffer.gpu);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            
        }, &m_svtBuffer.gpu);
    }


    /* Write descriptors */
    {
        DescriptorWriter writer;


        writer.BindSet(frame.descriptorSet);
        {
           // writer.Write(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.perFrameData);
            writer.Write(0, frame.voxelImage, m_sampler);
            writer.Write(1, frame.normalImage, m_sampler);
            writer.Write(2, m_stbnImage, m_sampler);    

            //writer.Write(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_svtBuffer.gpu);
            //writer.Write(2, frame.primaryImage, m_sampler);
        }
        writer.Update(*m_device);
    }

    {
        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM + VHM_SIZE * 4, VHM_SIZE * 16, 0);

        cmdBuffer.Barrier({

            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            
        }, &m_vhmBuffer);
    }

    /* Primary pass */
    {
        /*
        Attachment mainAttachment = {
            .Image = &frame.primaryImage,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
        };*/

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
        };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_primaryPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        cmdBuffer.EndRenderPass();
    }

    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &m_vhmBuffer);
    }

    /* Compute pass */
    {
        //vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoisingPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_denoisingPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoisingPass);

        vkCmdDispatch(cmdBuffer, VHM_SIZE / 1024, 1, 1);
    }

    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &m_vhmBuffer);
    }

    /* Transition images */
    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &frame.voxelImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cmdBuffer.Barrier(barrier, &frame.normalImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    /* Composite pass */
    {
        Attachment mainAttachment = {
            .Image = &dstImage,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
        };

        RenderPass renderPass;
        renderPass.Area = m_swapChain->GetArea();
        renderPass.Attachments = { mainAttachment };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_compositePass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        if (ImDrawData* drawData = ImGui::GetDrawData())
        {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
        }

        cmdBuffer.EndRenderPass();
    }
}


void MainScene::OnResizeEvent(int width, int height)
{
    createImages(width, height);
}


bool MainScene::OnKeyEvent(int key, int scancode, int action)
{
    auto window = m_app->GetWindow();

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        m_prevCursorPos = getCursorPosition();
        m_camera.active = !m_camera.active;

        if (m_camera.active)
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        else    
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    return false;
}


void MainScene::OnMouseMovedEvent(Vec2 pos)
{
    if (m_camera.active)
    {
        Vec2 movement = pos - m_prevCursorPos;
        m_prevCursorPos = pos;

        movement *= 0.002f;

        m_camera.yaw -= movement.x;
        m_camera.pitch -= movement.y;

        m_camera.pitch = glm::clamp(m_camera.pitch, -1.5f, 1.5f);

        Vec3 direction {
            cos(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.pitch),
        };
        m_camera.dir = direction;
    }
}


void MainScene::createImages(int width, int height)
{
    for (Frame& frame : m_frames)
    {
        auto size = m_swapChain->GetSize();
        if (width == 0 || height == 0)
            return;
        
        /*
        frame.primaryImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_B8G8R8A8_UNORM,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });*/

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
    }
}


VkShaderModule MainScene::loadShader(std::string_view path)
{
    auto code = FileSystem::ReadFile(path);
    if (!code)
    {
        LOG_ERROR("Failed to load shader: '{}'", path);
        return VK_NULL_HANDLE;
    }

    return m_device->CreateShaderModule(*code);
}


void MainScene::initImGui()
{
    auto window = m_app->GetWindow();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        LOG_ERROR("Failed to initialize imgui for glfw");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    //io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen7x14b.ttf", 14.0f);
    io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen8x16b.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen10x20b.ttf", 20.0f);

    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(*m_device, &poolInfo, nullptr, &imguiPool));

    VkFormat swapChainFormat = m_app->GetSwapChain().GetImageFormat();

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = m_device->GetInstance();
    info.PhysicalDevice = m_device->GetAdapter();
    info.Device = static_cast<VkDevice>(*m_device);
    info.QueueFamily = m_device->GetGraphicsQueueFamily();
    info.Queue = m_device->GetGraphicsQueue();
    info.DescriptorPool = imguiPool;
    info.MinImageCount = 3;
    info.ImageCount = 3;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainFormat;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.UseDynamicRendering = true;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        LOG_ERROR("Failed to initialize imgui for vulkan");
        return;
    }
}


void MainScene::deinitImGui()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}