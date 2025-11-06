#include "MainScene.h"
#include "Application.h"
#include "Base/Log.h"
#include "Base/FileSystem.h"
#include "Renderer/DescriptorSet.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>



struct PerFrameData
{
    float Color[3];
    float Tiling;
};



void MainScene::OnAttach()
{
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
            frameData->Color[0] = frameData->Color[1] = frameData->Color[2] = 0.5f;
            frameData->Tiling = 2.0f;

            descriptorAlloc.Add(&layout, &frame.descriptorSet);
        }

        m_descriptorPool = descriptorAlloc.Allocate(*m_device);
    }
    createImages();

    /* Load shaders */
    VkShaderModule fullscreenShader = loadShader("data/shaders/fullscreen.vert.spv");
    VkShaderModule primaryShader = loadShader("data/shaders/primary.frag.spv");
    VkShaderModule compositeShader = loadShader("data/shaders/composite.frag.spv");

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
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];
    {
        auto* data = reinterpret_cast<PerFrameData*>(frame.perFrameData.GetMapped());
        
        ImGui::Begin("Uniforms");
        ImGui::ColorEdit3("Color", data->Color);
        ImGui::SliderFloat("Tiling", &data->Tiling, 0.0f, 10.0f);
        ImGui::End();
    }
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
    createImages();
}


void MainScene::createImages()
{
    for (Frame& frame : m_frames)
    {
        frame.primaryImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_B8G8R8A8_UNORM,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = m_swapChain->GetSize(),
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