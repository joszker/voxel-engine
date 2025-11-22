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
};



void MainScene::OnAttach()
{
    m_camera.pos = Vec3(0.0f, 0.0f, -12.0f);
    m_camera.yaw = glm::half_pi<f32>();
    m_camera.dir = {
        cos(m_camera.yaw) * cos(m_camera.pitch),
        sin(m_camera.pitch),
        sin(m_camera.yaw) * cos(m_camera.pitch),
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
        layoutBuilder.Add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
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
}


void MainScene::OnDetach()
{
    deinitImGui();
}


void MainScene::OnUpdate(f32 dt)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];
    auto* frameData = reinterpret_cast<PerFrameData*>(frame.perFrameData.GetMapped());

    const Vec3 unitY = Vec3(0.0f, 1.0f, 0.0f);

    const Vec3 cameraRight = glm::normalize(glm::cross(unitY, m_camera.dir));
    const Vec3 cameraUp = glm::cross(m_camera.dir, cameraRight);


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

        m_camera.pos += 10.0f * (forward * m_camera.dir + right * cameraRight + upward * unitY) * dt;
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
    
    ImGui::Begin("Uniforms");
    ImGui::ColorEdit3("Voxel color", &frameData->Color.x);
    ImGui::SliderFloat3("Light position", &frameData->LightPos.x, -50.0f, +50.0f);
    ImGui::ColorEdit3("Light color", &frameData->LightColor.x);
    ImGui::ColorEdit3("Ambient color", &frameData->Ambient.x);
    ImGui::End();

    ImGui::Render();
}


void MainScene::OnRender(Image& dstImage, CommandBuffer& cmdBuffer)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];


    /* Write descriptors */
    {
        DescriptorWriter writer;

        writer.BindSet(frame.descriptorSet);
        {
            writer.Write(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.perFrameData);
            writer.Write(1, frame.primaryImage, m_sampler);
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
            sin(m_camera.pitch),
            sin(m_camera.yaw) * cos(m_camera.pitch),
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