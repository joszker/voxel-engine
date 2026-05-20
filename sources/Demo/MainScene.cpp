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

#include "Voxel/SVTree.h"

#define OGT_VOX_IMPLEMENTATION
#include "3rdparty/ogt_vox.h"

struct GPU_SVTree
{
    Vec3i dimensions;
    u32 root;
    u32 depth;
    VkDeviceAddress nodes;      // SVNode*
    VkDeviceAddress voxels;     // Voxel* 
};

struct PerFrameData
{
    Vec4 Resolution;
    Vec4 Color;
    Vec4 LightPos;
    Vec4 LightColor;
    Vec4 Ambient;
    Vec4 CameraPos;
    Vec4 CameraDir;
    Vec4 CameraRight;
    Vec4 CameraUp;

    GPU_SVTree Map;
};


Vec3 GetStepMask(Vec3 sideDist)
{
    glm::bvec3 mask;
    glm::bvec3 prio = glm::lessThan(sideDist, Vec3(sideDist.y, sideDist.z, sideDist.x));

    mask.x = prio.x && !prio.z;       // (T.x < T.y) && (T.x <= T.z)
    mask.y = prio.y && !prio.x;       // (T.y < T.z) && (T.y <= T.x)
    mask.z = !(mask.x || mask.y);

    return Vec3(mask);
}


Vec4 Trace(const SVTree& tree, Vec3 rayOrigin, Vec3 rayDir)
{
    rayDir = glm::normalize(rayDir);

    Vec4 color(0.0f);

    struct
    {
        u32 nodePtr;
        Vec3 nodePos;

    } stack[23];

    const Vec3 rayInvDir = 1.0f / rayDir;
    const Vec3 raySign = glm::sign(rayDir);
    const glm::bvec3 rayDirMask = glm::notEqual(rayDir, Vec3(0.0f));
    const Vec3 rayDirBias = Vec3(1e-4) * raySign;

    const Vec3 sideDistStep = glm::mix(Vec3(0.0f), rayInvDir, rayDirMask);

    /* ray aabb collision */
    Vec3 cellRayOrigin;
    {
        const Vec3 tMin = -rayOrigin * rayInvDir;
        const Vec3 tMax = (Vec3(tree.GetDimensions()) - rayOrigin) * rayInvDir;
        const Vec3 t1 = glm::min(tMin, tMax);
        const Vec3 t2 = glm::max(tMin, tMax);
        const float tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
        const float tFar = glm::min(glm::min(t2.x, t2.y), t2.z);

        if (tNear > tFar || tFar < 0.0)
            return Vec4(0); 

        cellRayOrigin = rayOrigin + glm::max(tNear, 0.0f) * rayDir;
        cellRayOrigin = glm::clamp(cellRayOrigin, Vec3(0), Vec3(tree.GetDimensions()));
    }
    Vec3 nodeRayOrigin = cellRayOrigin;

    f32 scale = f32(pow4(tree.GetDepth() - 1));
    Vec3 cellPos = scale * glm::floor((cellRayOrigin) / scale + rayDirBias);
    Vec3 sideDist = ((cellPos - cellRayOrigin) + scale * (0.5f + raySign * 0.5f)) * rayInvDir;

    uint level = tree.GetDepth() - 1;
    SVNode node = tree.GetRoot();
    u32 nodePtr = 0;
    Vec3 nodePos = Vec3(0.0f);

    for (int i = 0; i < 255; i++)
    {
        LOG_TRACE("{}[{},{},{}] | {}, {}, {}  ---  ({}, {}, {})", scale, nodePos.x, nodePos.y, nodePos.z, cellPos.x, cellPos.y, cellPos.z, cellRayOrigin.x, cellRayOrigin.y, cellRayOrigin.z);

        Vec3 cellIndex = glm::floor((cellPos - nodePos) / scale);

        if (glm::all(glm::greaterThanEqual(cellIndex, Vec3(0))) && glm::all(glm::lessThan(cellIndex, Vec3(4))))
        {
            uint childIndex = uint(glm::dot(cellIndex, Vec3(1, 4, 16)));

            while (!node.IsLeaf && node.IsOccupied(childIndex))
            {
                // Save node
                stack[level].nodePtr = nodePtr;
                stack[level].nodePos = nodePos;
                level -= 1;

                u32 childPtr = node.GetChildPtr(childIndex);
                node = tree.GetNodes()[childPtr];
                nodePtr = childPtr;
                nodePos = cellPos;
                scale /= 4;

                cellPos = scale * glm::floor((cellRayOrigin / scale + rayDirBias));
                sideDist = ((cellPos - cellRayOrigin) + scale * (0.5f + raySign * 0.5f)) * rayInvDir;
                sideDist = glm::mix(Vec3(glm::uintBitsToFloat(0x7F800000)), sideDist, rayDirMask);
                nodeRayOrigin = cellRayOrigin;

                cellIndex = glm::floor((cellPos - nodePos) / scale);
                childIndex = uint(glm::dot(cellIndex, Vec3(1, 4, 16)));

                LOG_TRACE("{}[{},{},{}] | {}, {}, {}  ---  ({}, {}, {})", scale, nodePos.x, nodePos.y, nodePos.z, cellPos.x, cellPos.y, cellPos.z, cellRayOrigin.x, cellRayOrigin.y, cellRayOrigin.z);
            }

            if (node.IsLeaf && node.IsOccupied(childIndex))
            {
                //color = Vec4(1.0, 0.0, 0.0, 1.0);
                color = Vec4(cellPos, 1.0);
                break;
            }
        }
        else
        {
            if (level >= tree.GetDepth() - 1)
                break;

            level += 1;
            nodePtr = stack[level].nodePtr;
            node = tree.GetNodes()[nodePtr];
            nodePos = stack[level].nodePos;

            scale *= 4;
            cellPos = scale * glm::floor((cellRayOrigin + 0.0f) / scale + rayDirBias);
            sideDist = ((cellPos - cellRayOrigin) + scale * (0.5f + raySign * 0.5f)) * rayInvDir;
            sideDist = glm::mix(Vec3(glm::uintBitsToFloat(0x7F800000)), sideDist, rayDirMask);
            nodeRayOrigin = cellRayOrigin;

            continue;
        }

        f32 minSideDist = glm::min(glm::min(sideDist.x, sideDist.y), sideDist.z);
        cellRayOrigin = nodeRayOrigin + minSideDist * rayDir;

        Vec3 step = GetStepMask(sideDist) * raySign * scale;
        cellPos += step;
        sideDist += step * sideDistStep;
    }

    LOG_TRACE("==============================================");

    return color;
}


void MainScene::OnAttach()
{
    auto voxData = FileSystem::ReadFile("data/Winter_Tree.vox");
    if (voxData)
    {
        auto *scene = ogt_vox_read_scene(voxData->data(), voxData->size());
        auto *model = scene->models[0];

        VoxelArray voxels{};
        voxels.Size = Vec3u(model->size_x, model->size_y, model->size_z);

        size_t size = model->size_x * model->size_y * model->size_z;
        for (size_t i = 0; i < size; i++)
            voxels.Data.push_back(Voxel{ model->voxel_data[i] });

        m_svt = SVTree(voxels);
    }
    else
    {
        const int h = 63;
        const int w = 63;
        std::vector<Voxel> voxels;
        voxels.resize(w * h * 1);

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float d = glm::distance(Vec2(float(x)/w, float(y)/h), Vec2(0.5));
                if (d <= 0.5f)
                    voxels[y * w + x] = Voxel { u8((2 * d * 255)) };
                else
                    voxels[y * w + x] = Voxel { 0 };
            }
        }

        m_svt = SVTree({ voxels, Vec3u(w, h, 1) });
    }

    //Trace(m_svt, Vec3(0.5, 5.5, 38.0), Vec3(0.0, 0.0, -1.0f));

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
        layoutBuilder.Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        layoutBuilder.Add(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        layoutBuilder.Add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layout = layoutBuilder.Build(*m_device);
    }


    {
        DescriptorAllocator descriptorAlloc;

        m_frames.resize(m_swapChain->GetFrameCount());
        for (Frame& frame : m_frames)
        {
            frame.perFrameData = m_device->CreateBuffer({
                .Size = sizeof(PerFrameData),
                .Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .AllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            });

            auto* frameData = reinterpret_cast<PerFrameData*>(frame.perFrameData.GetMapped());

            memset(frameData, 0, sizeof(frameData));
            frameData->Color[0] = frameData->Color[1] = frameData->Color[2] = 0.65f;
            frameData->CameraPos = Vec4(m_camera.pos, 0.0f);
            frameData->CameraDir = Vec4(m_camera.dir, 0.0f);

            frameData->LightPos = Vec4(10.0f, 10.0f, -3.0f, 1.0f);
            frameData->LightColor = Vec4(0.7f, 0.7f, 1.0f, 1.0f);
            frameData->Ambient = Vec4(0.2f, 0.2f, 0.2f, 1.0f);
            


            descriptorAlloc.Add(&layout, &frame.descriptorSet);
        }

        m_descriptorPool = descriptorAlloc.Allocate(*m_device);
    }
    createImages(m_swapChain->GetSize().width, m_swapChain->GetSize().height);

    /* Load shaders */
    VkShaderModule fullscreenShader = loadShader("data/shaders/fullscreen.vert.spv");
    VkShaderModule primaryShader = loadShader("data/shaders/primary.frag.spv");
    VkShaderModule compositeShader = loadShader("data/shaders/trace.frag.spv");

    /* Create primary pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(primaryShader);
        pipelineBuilder.AddColorAttachment({ m_frames[0].primaryImage.GetFormat() });
        pipelineBuilder.AddDescriptorLayout(layout);

        m_primaryPass = pipelineBuilder.Build(*m_device);
    }

    /* Create composite pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(compositeShader);
        pipelineBuilder.AddColorAttachment({ m_swapChain->GetImageFormat() });
        pipelineBuilder.AddDescriptorLayout(layout);

        m_compositePass = pipelineBuilder.Build(*m_device);
    }

    vkDestroyShaderModule(*m_device, fullscreenShader, nullptr);
    vkDestroyShaderModule(*m_device, primaryShader, nullptr);
    vkDestroyShaderModule(*m_device, compositeShader, nullptr);

    /* Storage buffers */
    {
        const size_t size = 20 * 1024 * 1024;

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
}


void MainScene::OnDetach()
{
    deinitImGui();
}


void MainScene::OnUpdate(f32 dt)
{

    {
        const Vec3 rayOrigin = m_camera.pos;
        const Vec3 rayInvDir = 1.0f / glm::normalize(m_camera.dir);

        const Vec3 tMin = (-rayOrigin) * rayInvDir;
        const Vec3 tMax = (Vec3(m_svt.GetDimensions()) - rayOrigin) * rayInvDir;
        const Vec3 t1 = glm::min(tMin, tMax);
        const Vec3 t2 = glm::max(tMin, tMax);
        const float tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
        const float tFar = glm::min(glm::min(t2.x, t2.y), t2.z);

        //LOG_TRACE("{} | {} | {}", tNear, tFar, tNear < 0.0f && tFar > 0.0f);
        //LOG_TRACE("{} | {}", tNear, tFar);
    }

    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];
    auto* frameData = reinterpret_cast<PerFrameData*>(frame.perFrameData.GetMapped());

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

        m_camera.pos += 10.0f * (forward * m_camera.dir + right * cameraRight + upward * upAxis) * dt;
    }


    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    int width, height;
    glfwGetFramebufferSize(m_app->GetWindow(), &width, &height);

    frameData->Resolution = Vec4(width, height, 0.0f, 0.0f);
    frameData->CameraPos = Vec4(m_camera.pos, 0.0f);
    frameData->CameraDir = Vec4(m_camera.dir, 0.0f);
    frameData->CameraRight = Vec4(cameraRight, 0.0f);
    frameData->CameraUp = Vec4(cameraUp, 0.0f);

#if 0    
    ImGui::Begin("Uniforms");
    ImGui::ColorEdit3("Voxel color", &frameData->Color.x);
    ImGui::SliderFloat3("Light position", &frameData->LightPos.x, -50.0f, +50.0f);
    ImGui::ColorEdit3("Light color", &frameData->LightColor.x);
    ImGui::ColorEdit3("Ambient color", &frameData->Ambient.x);
    ImGui::End();
#endif

    {
        auto imViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(imViewport->WorkPos, ImGuiCond_Always);

        auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::Begin("overlay", 0, flags);

        ImGui::Text("X: %.3f", m_camera.pos.x);
        ImGui::Text("Y: %.3f", m_camera.pos.y);
        ImGui::Text("Z: %.3f", m_camera.pos.z);

        ImGui::End();
    }


    ImGui::Render();

    frameData->Map.nodes = m_svtBuffer.gpu.GetDeviceAddress();
    frameData->Map.voxels = frameData->Map.nodes + sizeof(SVNode) * m_svt.GetNodes().size();
    frameData->Map.depth = m_svt.GetDepth();
    frameData->Map.dimensions = m_svt.GetDimensions();
}


void MainScene::OnRender(Image& dstImage, CommandBuffer& cmdBuffer)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];


    if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_T) == GLFW_PRESS)
    {
        m_swapChain->WaitForPreviousFrames();
      


        constexpr size_t k_dataSize = 20 * 1024 * 1024;
        static byte *data = nullptr;
        if (!data) data = new byte[k_dataSize];

        {
            auto srcNodes = m_svt.GetNodes();
            auto srcVoxels = m_svt.GetVoxels();

            SVNode *dstNodes = (SVNode*)data;
            Voxel *dstVoxels = (Voxel*)(dstNodes + srcNodes.size());

            memcpy(dstNodes, srcNodes.data(), sizeof(SVNode) * srcNodes.size());
            memcpy(dstVoxels, srcVoxels.data(), sizeof(Voxel) * srcVoxels.size());

            m_device->Write(m_svtBuffer.cpu, 0, data, k_dataSize);
        }
        //delete[] data;
       

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
            writer.Write(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.perFrameData);
            writer.Write(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_svtBuffer.gpu);
            writer.Write(2, frame.primaryImage, m_sampler);
        }
        writer.Update(*m_device);
    }

    /* Begin primary pass */
    {
        Attachment mainAttachment = {
            .Image = &frame.primaryImage,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
        };

        RenderPass renderPass;
        renderPass.Area = frame.primaryImage.GetArea(),
        renderPass.Attachments = { mainAttachment };

        cmdBuffer.BeginRenderPass(renderPass);
    }

    /* Draw primary pass */
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        cmdBuffer.EndRenderPass();
    }

    /* Transition primary image */
    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &frame.primaryImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    /* Begin composite pass */
    {
        Attachment mainAttachment = {
            .Image = &dstImage,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
        };

        RenderPass renderPass;
        renderPass.Area = m_swapChain->GetArea();
        renderPass.Attachments = { mainAttachment };

        cmdBuffer.BeginRenderPass(renderPass);
    }

    /* Draw composite pass */
    {
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        if (ImDrawData* drawData = ImGui::GetDrawData())
        {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
        }
    }

    cmdBuffer.EndRenderPass();
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

    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        Vec4 result = Trace(m_svt, m_camera.pos, m_camera.dir);

        //LOG_TRACE("Ray trace: {}, {}, {}, {}", result.x, result.y, result.z, result.w);
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
        
        frame.primaryImage = m_device->CreateImage({
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
    auto code = *FileSystem::ReadFile(path);

    return m_device->CreateShaderModule(code);
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