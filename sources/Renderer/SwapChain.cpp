#include "SwapChain.h"
#include "Base/Log.h"

#include <GLFW/glfw3.h>


SwapChain::SwapChain(RenderDevice& device)
    : m_device(device), m_frameIndex(0), m_imageIndex(0)
{

}


void SwapChain::Init(struct GLFWwindow* window, uint32_t maxFramesInFlight)
{
    m_window = window;

    /* Create command buffers */
    std::vector<VkCommandBuffer> cmdBuffers(maxFramesInFlight);
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_device.GetGraphicsCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = cmdBuffers.size();

        VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, cmdBuffers.data()));
    }

    /* Initialize per frame resources */
    m_frames.resize(size_t(maxFramesInFlight));
    for (size_t i = 0; i < m_frames.size(); i++)
    {
        m_frames[i].imageAvailable = m_device.MakeSemaphore();
        m_frames[i].renderFinished = m_device.MakeSemaphore();
        m_frames[i].inFlight = m_device.MakeFence(true);
        m_frames[i].cmdBuffer = CommandBuffer(cmdBuffers[i]);
    }

    Recreate();
}


void SwapChain::Recreate()
{
    if (m_handle)
    {
        /* Wait for all outstanding queue operations to complete */
        vkDeviceWaitIdle(m_device);

        /* Destroy swap chain image views */
        for (Image& image : m_images)
        {
            m_device.DestroyImage(image);
        }
        m_images.clear();

        vkDestroySwapchainKHR(m_device, m_handle, NULL);
        m_handle = VK_NULL_HANDLE;
    }

    /* Create swap chain */
    VkSwapchainCreateInfoKHR scInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    std::vector<VkImage> swapImages;
    {
        VkPhysicalDevice adapter = m_device.GetAdapter();
        VkSurfaceKHR surface = m_device.GetSurface();

        VkSurfaceCapabilitiesKHR surfaceCaps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(adapter, surface, &surfaceCaps));

        /* Get surface formats */
        uint32_t numSurfaceFormats;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(adapter, surface, &numSurfaceFormats, NULL));
        std::vector<VkSurfaceFormatKHR> surfaceFormats;
        surfaceFormats.resize(numSurfaceFormats);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(adapter, surface, &numSurfaceFormats, surfaceFormats.data()));

        /* Get present modes */
        uint32_t numPresentModes;
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(adapter, surface, &numPresentModes, NULL));
        std::vector<VkPresentModeKHR> presentModes;
        presentModes.resize(numPresentModes);
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(adapter, surface, &numPresentModes, presentModes.data()));
        
        /* Choose image format */
        m_format = VK_FORMAT_UNDEFINED;
        for (auto surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {   
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB)
                {
                    m_format = VK_FORMAT_B8G8R8A8_SRGB;
                    break;
                }

                if (m_format == VK_FORMAT_UNDEFINED)
                    m_format = surfaceFormat.format;
            }
        }
        m_format = VK_FORMAT_B8G8R8A8_UNORM;

        /* Choose present mode */
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto mode : presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = mode; 
                break;
            }
        }

        /* Determine image extent */
        if (surfaceCaps.currentExtent.width == 0xFFFFFFFF)
        {   
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);

            m_extent.width = std::min(std::max((uint32_t)width, surfaceCaps.minImageExtent.width), surfaceCaps.maxImageExtent.width);
            m_extent.height = std::min(std::max((uint32_t)height, surfaceCaps.minImageExtent.height), surfaceCaps.maxImageExtent.height);
        }
        else
        {
            m_extent = surfaceCaps.currentExtent;
        }

        uint32_t imageCount = surfaceCaps.minImageCount + 1;
        imageCount = std::min(imageCount, surfaceCaps.maxImageCount);

        /* Create swap chain */
        scInfo.surface = surface;
        scInfo.minImageCount = imageCount;
        scInfo.imageFormat = m_format;
        scInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        scInfo.imageExtent = m_extent;
        scInfo.imageArrayLayers = 1;
        scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        scInfo.preTransform = surfaceCaps.currentTransform;
        scInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        scInfo.presentMode = presentMode;
        scInfo.clipped = VK_TRUE;

        uint32_t graphicsQueueFamily = m_device.GetGraphicsQueueFamily();
        uint32_t presentQueueFamily = m_device.GetPresentQueueFamily();

        uint32_t queueFamilies[] = { graphicsQueueFamily, presentQueueFamily };
        if (graphicsQueueFamily != presentQueueFamily)
        {
            scInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            scInfo.queueFamilyIndexCount = 2;
            scInfo.pQueueFamilyIndices = queueFamilies;
        }
        else
        {
            scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            scInfo.queueFamilyIndexCount = 0;
            scInfo.pQueueFamilyIndices = NULL;
        }

        VK_CHECK(vkCreateSwapchainKHR(m_device, &scInfo, NULL, &m_handle));

        /* Get swap chain images */
        uint32_t numSwapChainImages;
        VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_handle, &numSwapChainImages, NULL));
        swapImages.resize(numSwapChainImages);
        VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_handle, &numSwapChainImages, swapImages.data()));
    }

    /* Create swap chain image views */
    {
        m_images.resize(swapImages.size());
        for (size_t i = 0; i < swapImages.size(); i++)
        {
            m_images[i] = m_device.CreateImage(scInfo, swapImages[i]);
        }
    }
}


Image* SwapChain::AcquireImage(CommandBuffer *cmdBuffer)
{
    Frame& currentFrame = m_frames[m_frameIndex];

    VK_CHECK(vkWaitForFences(m_device, 1, &currentFrame.inFlight, VK_TRUE, UINT64_MAX));

    /* Get image index */
    {
        VkResult result = vkAcquireNextImageKHR(m_device, m_handle, UINT64_MAX, currentFrame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            Recreate();
            return AcquireImage(cmdBuffer);
        }
        else if (result == VK_SUBOPTIMAL_KHR)
        {
            LOG_WARNING("Suboptimal swap chain");
        }
        else if (result != VK_SUCCESS)
        {
            LOG_ERROR("Failed to acquire swap chain image");
            return nullptr;
        }
    }

    /* Only reset after image has been acquired to prevent deadlock */
    VK_CHECK(vkResetFences(m_device, 1, &currentFrame.inFlight));

    vkResetCommandBuffer(currentFrame.cmdBuffer,  0);
    *cmdBuffer = currentFrame.cmdBuffer;

    return &m_images[m_imageIndex];
}


void SwapChain::Submit(VkCommandBuffer cmds)
{
    /* Submit command buffer */
    Frame& currentFrame = m_frames[m_frameIndex];

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &currentFrame.imageAvailable;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmds;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &currentFrame.renderFinished;

    VK_CHECK(vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submitInfo, currentFrame.inFlight));
}


void SwapChain::Present()
{
    Frame& currentFrame = m_frames[m_frameIndex];

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_handle;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_device.GetPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        LOG_ERROR("Failed to present swap chain image");
        return;
    }
    m_frameIndex += 1;
    m_frameIndex %= uint32_t(m_frames.size());
}