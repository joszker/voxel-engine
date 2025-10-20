#include "RenderDevice.h"
#include "Base/Log.h"

#include <GLFW/glfw3.h>
#include <vector>
#include <set>
#include <array>




bool RenderDevice::Init(struct GLFWwindow* window, bool debug)
{
    /* Get available instance layers */
    std::vector<VkLayerProperties> instanceLayers;
    {   
        uint32_t numLayers = 0;
        vkEnumerateInstanceLayerProperties(&numLayers, nullptr);
        instanceLayers.resize(numLayers);
        vkEnumerateInstanceLayerProperties(&numLayers, instanceLayers.data());
    }

    /* Create instance */
    {
        VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        appInfo.pApplicationName = "Voxel";
        appInfo.applicationVersion = 0;
        appInfo.pEngineName = "Voxel";
        appInfo.engineVersion = 0;
        appInfo.apiVersion = VK_API_VERSION_1_3;

        /* Get required extensions */
        uint32_t numRequiredExtensions;
        const char **requiredExtensions = glfwGetRequiredInstanceExtensions(&numRequiredExtensions);        
        std::vector<const char*> extensions(numRequiredExtensions + 2);
        extensions.resize(numRequiredExtensions);
        memcpy(extensions.data(), requiredExtensions, numRequiredExtensions * sizeof(*requiredExtensions));

        /* Add optional extensions */
        if (debug)
        {
            extensions.push_back("VK_EXT_debug_utils");
            extensions.push_back("VK_EXT_debug_report");
        }

        /* Instance layers */
        std::array layers = { "VK_LAYER_KHRONOS_validation" };
        
        /* Debug messenger */
        VkDebugUtilsMessengerCreateInfoEXT dumInfo = {};
        dumInfo.sType =  VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dumInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        dumInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;      
        dumInfo.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        dumInfo.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;        
        dumInfo.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dumInfo.pfnUserCallback = RenderDevice::debugMessageHandler;
        dumInfo.pUserData = this;

        VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pNext = &dumInfo;
        info.pApplicationInfo = &appInfo;
        info.enabledExtensionCount = extensions.size();
        info.ppEnabledExtensionNames = extensions.data();

        if (debug)
        {
            /* Enabled validation layers and debug messenger */
            info.pNext = &dumInfo;
            info.enabledLayerCount = layers.size();
            info.ppEnabledLayerNames = layers.data();
        }

        VK_CHECK(vkCreateInstance(&info, NULL, &m_instance));
    }

    /* Create surface */
    {
        if (glfwCreateWindowSurface(m_instance, window, NULL, &m_surface) != VK_SUCCESS)
            return false;
    }

    /* Choose physical device (adapter) */
    {
        uint32_t numAdapters;
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &numAdapters, NULL));

        std::vector<VkPhysicalDevice> adapters;
        adapters.resize(numAdapters);
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &numAdapters, adapters.data()));

        m_adapter = adapters[0];    // Choose first adapter by default
        size_t adapterIndex = 0;
        for (const auto adapter : adapters)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(adapter, &props);

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_adapter = adapters[adapterIndex];
                break;
            }
            adapterIndex++;
        }

        vkGetPhysicalDeviceFeatures(m_adapter, &m_adapterFeatures);

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_adapter, &props);

        LOG_INFO("Device Name: {}", props.deviceName); 
    }

    /* Choose device queue families */
    {
        std::vector<VkQueueFamilyProperties> queueFamilies;
        uint32_t numQueueFamilies;
        vkGetPhysicalDeviceQueueFamilyProperties(m_adapter, &numQueueFamilies, NULL);
        queueFamilies.resize(numQueueFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties(m_adapter, &numQueueFamilies, queueFamilies.data());
        
        VkBool32 foundPresentFamily = VK_FALSE;
        VkBool32 foundGraphicsFamily = VK_FALSE;
        size_t index = 0;
        for (auto queueFamily : queueFamilies)
        {   
            if (debug)
            {
                std::string flags = "";
                if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) flags += " GRAPHICS";
                if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) flags += " COMPUTE";
                if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) flags += " TRANSFER";

                LOG_INFO("\tQueue[{}]: #{} {}", index, queueFamily.queueCount, flags);
            }

            if (!foundPresentFamily)
            {
                VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(m_adapter, index, m_surface, &foundPresentFamily);
                if (result == VK_SUCCESS && foundPresentFamily)
                    m_presentQueueFamily = index;
            }

            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                foundGraphicsFamily = VK_TRUE;
                m_graphicsQueueFamily = index;
            }
            index++;
        }
        if (!foundPresentFamily || !foundGraphicsFamily)
        {
            return false;
        }

        //m_computeQueueFamily = 0;
        //m_transferQueueFamily = 0;
    }

    /* Create logical device */
    {
        float queuePriorities[1] = { 1.0f };
        VkDeviceQueueCreateInfo queueInfos[3] = {};
        std::set<uint32_t> queueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily };
        int i = 0;
        for (uint32_t queueFamily : queueFamilies)
        {
            queueInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfos[i].queueFamilyIndex = queueFamily;
            queueInfos[i].queueCount = 1;
            queueInfos[i].pQueuePriorities = queuePriorities;
            ++i;
        }

        std::array extensions = { "VK_KHR_swapchain" };

        const VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            NULL, VK_TRUE
        };

        VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.pNext = &dynamicRenderingFeatures;
        info.pQueueCreateInfos = queueInfos;
        info.queueCreateInfoCount = queueFamilies.size();
        info.pEnabledFeatures = &m_adapterFeatures;
        info.enabledExtensionCount = extensions.size();
        info.ppEnabledExtensionNames = extensions.data();

        VK_CHECK(vkCreateDevice(m_adapter, &info, NULL, &m_device));
    }

    /* Get device queues */
    {
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
    }

    LOG_INFO("Graphics Queue = {}", (void*)m_graphicsQueue);
    LOG_INFO(" Present Queue = {}", (void*)m_presentQueue);

    /* Create swap chain */
    {
        ResizeSwapChain(window);
    }

    return true;
}


void RenderDevice::ResizeSwapChain(struct GLFWwindow* window)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    ResizeSwapChain(uint32_t(width), uint32_t(height));    
}


void RenderDevice::ResizeSwapChain(uint32_t width, uint32_t height)
{
    if (m_swapChain)
    {
        /* Wait for all outstanding queue operations to complete */
        vkDeviceWaitIdle(m_device);

        /* Destroy swap chain image views */
        for (VkImageView swapImageView : m_swapImageViews)
        {
            vkDestroyImageView(m_device, swapImageView, NULL);
        }
        m_swapImageViews.clear();

        vkDestroySwapchainKHR(m_device, m_swapChain, NULL);
        m_swapChain = VK_NULL_HANDLE;
        m_swapImages.clear();
    }

    /* Create swap chain and swap images */
    {
        VkSurfaceCapabilitiesKHR surfaceCaps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_adapter, m_surface, &surfaceCaps));

        /* Get surface formats */
        uint32_t numSurfaceFormats;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_adapter, m_surface, &numSurfaceFormats, NULL));
        std::vector<VkSurfaceFormatKHR> surfaceFormats;
        surfaceFormats.resize(numSurfaceFormats);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_adapter, m_surface, &numSurfaceFormats, surfaceFormats.data()));

        /* Get present modes */
        uint32_t numPresentModes;
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_adapter, m_surface, &numPresentModes, NULL));
        std::vector<VkPresentModeKHR> presentModes;
        presentModes.resize(numPresentModes);
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_adapter, m_surface, &numPresentModes, presentModes.data()));
        
        /* Choose image format */
        m_swapImageFormat = VK_FORMAT_UNDEFINED;
        for (auto surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {   
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB)
                {
                    m_swapImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
                    break;
                }

                if (m_swapImageFormat == VK_FORMAT_UNDEFINED)
                    m_swapImageFormat = surfaceFormat.format;
            }
        }
        m_swapImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

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
            m_swapChainExtent.width = std::min(std::max((uint32_t)width, surfaceCaps.minImageExtent.width), surfaceCaps.maxImageExtent.width);
            m_swapChainExtent.height = std::min(std::max((uint32_t)height, surfaceCaps.minImageExtent.height), surfaceCaps.maxImageExtent.height);
        }
        else
        {
            m_swapChainExtent = surfaceCaps.currentExtent;
        }

        uint32_t imageCount = surfaceCaps.minImageCount + 1;
        imageCount = std::min(imageCount, surfaceCaps.maxImageCount);

        VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = m_surface;
        info.minImageCount = imageCount;
        info.imageFormat = m_swapImageFormat;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent = m_swapChainExtent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.preTransform = surfaceCaps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = presentMode;
        info.clipped = VK_TRUE;

        uint32_t queueFamilyIndices[] = { m_graphicsQueueFamily, m_presentQueueFamily };
        if (m_graphicsQueueFamily != m_presentQueueFamily)
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VK_CHECK(vkCreateSwapchainKHR(m_device, &info, NULL, &m_swapChain));

        /* Get swap chain images */
        uint32_t numSwapChainImages;
        VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapChain, &numSwapChainImages, NULL));
        m_swapImages.resize(numSwapChainImages);
        VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapChain, &numSwapChainImages, m_swapImages.data()));
    }

    /* Create swap chain image views */
    {
        m_swapImageViews.resize(m_swapImages.size());

        for (size_t i = 0; i < m_swapImages.size(); i++)
        {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = m_swapImages[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = m_swapImageFormat;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(m_device, &info, NULL, &m_swapImageViews[i]));
        }
    }
}


VkBool32 RenderDevice::debugMessageHandler(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             types,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData) 
{
    auto device = reinterpret_cast<RenderDevice*>(pUserData);

    int logLevel = LOG_LEVEL_INFO;
    switch (severity)
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            logLevel = LOG_LEVEL_TRACE;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            logLevel = LOG_LEVEL_INFO;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            logLevel = LOG_LEVEL_WARNING;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            logLevel = LOG_LEVEL_ERROR;
            break;        
    }

    LOG_GENERIC(logLevel, "[Vulkan API] {}", pCallbackData->pMessage);

    return VK_FALSE;
}


VkShaderModule RenderDevice::CreateShaderModule(std::span<const byte> code) const
{
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(m_device, &info, nullptr, &shaderModule));

    return shaderModule;
}


VkSemaphore RenderDevice::MakeSemaphore() const
{
    VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    VkSemaphore semaphore;
    VK_CHECK(vkCreateSemaphore(m_device, &info, nullptr, &semaphore));
    return semaphore;
}


VkFence RenderDevice::MakeFence(bool signaled) const
{
    VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (signaled)
        info.flags |= VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence fence;
    VK_CHECK(vkCreateFence(m_device, &info, nullptr, &fence));
    return fence;
}