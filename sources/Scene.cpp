#include "Scene.h"
#include "Renderer/PipelineBuilder.h"

#include "Base/FileSystem.h"


Scene::Scene()
{

}


void Scene::Init(struct GLFWwindow *window)
{
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
    VkSemaphore imageAvailable = m_imageAvailable[m_frameIndex];
    VkSemaphore renderFinished = m_renderFinished[m_frameIndex];
    VkFence inFlight = m_inFlight[m_frameIndex];


    VK_CHECK(vkWaitForFences(m_device, 1, &inFlight, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_device, 1, &inFlight));

    uint32_t imageIndex;
    VK_CHECK(vkAcquireNextImageKHR(m_device, m_device.GetSwapChain(), UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex));

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
    vkCmdEndRendering(cmdBuffer);

    MakeImagePresentable(cmdBuffer, image);

    VK_CHECK(vkEndCommandBuffer(cmdBuffer));
}