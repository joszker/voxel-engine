#include "Scene.h"
#include "Renderer/PipelineBuilder.h"

#include "Base/FileSystem.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>


Scene::Scene()
{

}


void Scene::Init(struct GLFWwindow *window)
{
    m_window = window;
    m_device.Init(window, true);

    /* Create pipeline */
    {
        PipelineBuilder pipelineBuilder;

        auto vertexShaderCode = *FS::ReadFile("data/fullscreen.vert.spv");
        auto fragmentShaderCode = *FS::ReadFile("data/fullscreen.frag.spv");

        VkShaderModule vertexShader = m_device.CreateShaderModule(vertexShaderCode);
        VkShaderModule fragmentShader = m_device.CreateShaderModule(fragmentShaderCode);
        
        pipelineBuilder.AddVertexShader(vertexShader);
        pipelineBuilder.AddFragmentShader(fragmentShader);

        ColorAttachment color;
        color.BlendState.colorWriteMask = 
            VK_COLOR_COMPONENT_R_BIT | 
            VK_COLOR_COMPONENT_G_BIT | 
            VK_COLOR_COMPONENT_B_BIT | 
            VK_COLOR_COMPONENT_A_BIT;
        color.BlendState.blendEnable = VK_FALSE;
        color.Format = m_device.GetSwapChainImageFormat();

        pipelineBuilder.SetColorAttachments({ color });

        m_pipeline = pipelineBuilder.Build(m_device);
    }

    /* Init ImGui */
    {
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
	    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &imguiPool));

        VkFormat swapChainFormat = m_device.GetSwapChainImageFormat();

        ImGui_ImplVulkan_InitInfo info = {};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = m_device.GetInstance();
        info.PhysicalDevice = m_device.GetAdapter();
        info.Device = (VkDevice)m_device;
        info.QueueFamily = m_device.GetGraphicsQueueFamily();
        info.Queue = m_device.GetGraphicsQueue();
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

        ImGuiIO& io = ImGui::GetIO();
        //io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen8x16b.ttf", 16.0f);
    }

    /* Create command pool */
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = m_device.GetGraphicsQueueFamily();

        VK_CHECK(vkCreateCommandPool(m_device, &info, nullptr, &m_cmdPool));
    }

    /* Create command buffers */
    {
        m_cmdBuffers.resize(m_device.GetSwapChainImageCount());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = m_cmdBuffers.size();

        VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, m_cmdBuffers.data()));
    }

    /* Record command buffers */
    for (size_t imageIndex = 0; imageIndex < m_cmdBuffers.size(); imageIndex++)
    {
        recordCommandBuffer(imageIndex);
    }

    /* Synchronization */
    for (size_t i = 0; i < k_maxFramesInFlight; i++)
    {
        m_imageAvailable.push_back(m_device.MakeSemaphore());
        m_renderFinished.push_back(m_device.MakeSemaphore());
        m_inFlight.push_back(m_device.MakeFence(true));
    }
}


void Scene::Render()
{
    /* ImGui */
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();
        ImGui::ShowDemoWindow();
        ImGui::Render();
    }


    VkSemaphore imageAvailable = m_imageAvailable[m_frameIndex];
    VkSemaphore renderFinished = m_renderFinished[m_frameIndex];
    VkFence inFlight = m_inFlight[m_frameIndex];

    VK_CHECK(vkWaitForFences(m_device, 1, &inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex;
    {
        VkResult result = vkAcquireNextImageKHR(m_device, m_device.GetSwapChain(), UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_device.ResizeSwapChain(m_window);
            return;
        }
        else if (result == VK_SUBOPTIMAL_KHR)
        {
            LOG_WARNING("Suboptimal swap chain");
        }
        else if (result != VK_SUCCESS)
        {
            LOG_ERROR("Failed to present swap chain image");
            return;
        }
    }
    VK_CHECK(vkResetFences(m_device, 1, &inFlight));

    /* Record command buffer */
    {
        vkResetCommandBuffer(m_cmdBuffers[imageIndex],  0);
        recordCommandBuffer(imageIndex);
    }


    VkSemaphore waitSemaphores[] = { imageAvailable };
    VkSemaphore signalSemaphores[] = { renderFinished };

    /* Submit command buffer */
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_cmdBuffers[imageIndex];

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VK_CHECK(vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submitInfo, inFlight));
    }

    /* Presentation */
    {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { m_device.GetSwapChain() };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(m_device.GetPresentQueue(), &presentInfo);
    }
    m_frameIndex += 1;
    m_frameIndex %= k_maxFramesInFlight;
}


void Scene::OnResize(uint width, uint height)
{

}


void Scene::recordCommandBuffer(size_t imageIndex)
{
    VkCommandBuffer cmdBuffer = m_cmdBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));

    VkImage image = m_device.GetSwapChainImages()[imageIndex];
    VkImageView imageView = m_device.GetSwapChainImageViews()[imageIndex];

    MakeImageRenderable(cmdBuffer, image);
    {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = m_device.GetSwapChainImageViews()[imageIndex];
        attachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f }};


        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = m_device.GetSwapChainArea();
        renderInfo.layerCount = 1;
        renderInfo.viewMask;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &attachment;
        renderInfo.pDepthAttachment = nullptr;
        renderInfo.pStencilAttachment = nullptr;
        
        vkCmdBeginRendering(cmdBuffer, &renderInfo);
    }

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    VkExtent2D swapChainExtent = m_device.GetSwapChainExtent();
    {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = float(swapChainExtent.width);
        viewport.height = float(swapChainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent;
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    }

    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

    /* Render ImGui */
    if (ImDrawData* drawData = ImGui::GetDrawData())
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
    }
    

    vkCmdEndRendering(cmdBuffer);
    MakeImagePresentable(cmdBuffer, image);

    VK_CHECK(vkEndCommandBuffer(cmdBuffer));
}