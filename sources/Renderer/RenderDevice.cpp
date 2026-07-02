#include "RenderDevice.h"
#include "Base/Log.h"
#include "CommandBuffer.h"

#include <GLFW/glfw3.h>
#include <vector>
#include <set>
#include <array>
#include <functional>


#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>


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
        VK_CHECK(glfwCreateWindowSurface(m_instance, window, NULL, &m_surface));
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
        VkBool32 foundTransferFamily = VK_FALSE;

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

                ASSERT(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT);
            }
            else if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                /* Found dedicated compute queue... */
            }
            else if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                /* Found dedicated transfer queue */
                foundTransferFamily = VK_TRUE;
                m_transferQueueFamily = index;
            }

            index++;
        }
        if (!foundPresentFamily || !foundGraphicsFamily)
        {
            return false;
        }
        if (!foundTransferFamily)
        {
            m_transferQueueFamily = m_graphicsQueueFamily;
        }
    }

    /* Create logical device */
    {
        float queuePriorities[1] = { 1.0f };
        VkDeviceQueueCreateInfo queueInfos[3] = {};
        std::set<uint32_t> queueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily, m_transferQueueFamily };
        int i = 0;
        for (uint32_t queueFamily : queueFamilies)
        {
            queueInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfos[i].queueFamilyIndex = queueFamily;
            queueInfos[i].queueCount = 1;
            queueInfos[i].pQueuePriorities = queuePriorities;
            ++i;
        }

        std::array extensions = { "VK_KHR_swapchain", "VK_EXT_shader_atomic_float" };


        /* Enable synchronization 2 */
        VkPhysicalDeviceSynchronization2Features sync2Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .synchronization2 = VK_TRUE,
        };

        /* Enable shader atomic float operations */
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
            .shaderBufferFloat32Atomics = VK_TRUE,
            .shaderBufferFloat32AtomicAdd = VK_TRUE,
        };

        /* Enable dynamic rendering */
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .dynamicRendering = VK_TRUE,
        };
        
        /*
        VkPhysicalDeviceMaintenance9FeaturesKHR m9Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR,
            .maintenance9 = VK_TRUE,
        };*/

        /* Enable vulkan 1.2 device features */
        VkPhysicalDeviceVulkan12Features vk12Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .storageBuffer8BitAccess = VK_TRUE,
            .uniformAndStorageBuffer8BitAccess = VK_TRUE,
            .shaderInt8 = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE,
        };

        
        atomicFloatFeatures.pNext = &sync2Features;
        dynamicRenderingFeatures.pNext = &atomicFloatFeatures;
        vk12Features.pNext = &dynamicRenderingFeatures;

        VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.pNext = &vk12Features;
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
        vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);
    }

    LOG_INFO("Graphics Queue = {}", (void*)m_graphicsQueue);
    LOG_INFO(" Present Queue = {}", (void*)m_presentQueue);


    /* Create memory allocator */
    {
        VmaAllocatorCreateInfo info = {};
        info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        info.physicalDevice = m_adapter;
        info.device = m_device;
        info.instance = m_instance;
        info.vulkanApiVersion = VK_API_VERSION_1_3;

        VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
    }


    /* Create graphics command pool */
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = m_graphicsQueueFamily;

        VK_CHECK(vkCreateCommandPool(m_device, &info, nullptr, &m_graphicsCmdPool));
    }

    if (hasDedicatedTransferQueue())
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = m_transferQueueFamily;

        VK_CHECK(vkCreateCommandPool(m_device, &info, nullptr, &m_transferCmdPool));        
    }
    else
    {
        m_transferCmdPool = m_graphicsCmdPool;
    }

    /* Create transfer command buffer */
    {
        VkCommandBufferAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_transferCmdPool,
            .commandBufferCount = 1,
        };
        VK_CHECK(vkAllocateCommandBuffers(m_device, &info, &m_transferCmdBuffer));

        m_transferPending = MakeFence(true);
    }

    return true;
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


void RenderDevice::DestroySemaphore(VkSemaphore semaphore) const
{
    vkDestroySemaphore(m_device, semaphore, nullptr);
}


Buffer RenderDevice::CreateBuffer(const BufferDesc& desc)
{
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = 0,
        .size = desc.Size,
        .usage = desc.Usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc = {
        .flags = desc.AllocFlags,
        .usage = desc.AllocType,
        .requiredFlags = desc.RequiredFlags,
        .preferredFlags = desc.PreferredFlags,
    };

    VmaAllocationInfo memory;
    VmaAllocation allocation;
    VkBuffer buffer;
    VK_CHECK(vmaCreateBuffer(m_allocator, &info, &alloc, &buffer, &allocation, &memory));

    VkDeviceAddress deviceAddress = 0;
    if (desc.Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        VkBufferDeviceAddressInfo addressInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
            .buffer = buffer,
        };
        deviceAddress = vkGetBufferDeviceAddress(m_device, &addressInfo);
        ASSERT(deviceAddress != 0);
    }

    return Buffer(allocation, buffer, info.size, info.usage, memory.pMappedData, deviceAddress);
}


void RenderDevice::DestroyBuffer(const Buffer& buffer)
{
    vmaDestroyBuffer(m_allocator, buffer, buffer.GetAllocation());
}

void RenderDevice::Write(Buffer *buffer, size_t dstOffset, const void *src, size_t numBytes)
{
    ASSERT(dstOffset + numBytes <= buffer->GetSize());

    VmaAllocation allocation = buffer->GetAllocation();

    void *mapped;
    vmaMapMemory(m_allocator, allocation, &mapped);

    memcpy((u8*)mapped + dstOffset, src, numBytes);

    VkMemoryPropertyFlags memoryProps;
    vmaGetAllocationMemoryProperties(m_allocator, allocation, &memoryProps);
    if (memoryProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT == 0)
    {
        // Flush required for non-coherent memory.
        vmaFlushAllocation(m_allocator, allocation, dstOffset, numBytes);
    }

    vmaUnmapMemory(m_allocator, allocation);
}


Image RenderDevice::CreateImage(const VkSwapchainCreateInfoKHR& scInfo, VkImage image)
{
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = scInfo.imageFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(m_device, &info, nullptr, &imageView));

    ImageDesc desc = {
        .Type = VK_IMAGE_TYPE_2D,
        .Format = scInfo.imageFormat,
        .Usage = scInfo.imageUsage,
        .Size = { scInfo.imageExtent.width, scInfo.imageExtent.height, 0 },
        .Layers = scInfo.imageArrayLayers,
        .ViewType = VK_IMAGE_VIEW_TYPE_2D,
    };
    
    return Image(VMA_NULL, image, imageView, desc, info.subresourceRange);
}


Image RenderDevice::CreateImage(const ImageDesc& desc)
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    switch (desc.Format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT: 
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;

        case VK_FORMAT_S8_UINT:
            aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
            break;

        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
    }

    VkImageSubresourceRange subresourceRange = {
        .aspectMask = aspect,
        .baseMipLevel = 0,
        .levelCount = desc.MipLevels,
        .baseArrayLayer = 0,
        .layerCount = desc.Layers,
    };

    return CreateImage(desc, subresourceRange);
}



Image RenderDevice::CreateImage(ImageDesc desc, const VkImageSubresourceRange& subresRange)
{
    const uint32_t mipLevelLimit = std::bit_width(std::max(std::max(desc.Size.width, desc.Size.height), desc.Size.depth));
    desc.MipLevels = std::min(desc.MipLevels, mipLevelLimit);

    /* Create image */
    VkImage image;
    VmaAllocation allocation;
    {
        VkImageCreateInfo info = { 
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = desc.Type,
            .format = desc.Format,
            .extent = desc.Size,
            .mipLevels = desc.MipLevels,
            .arrayLayers = desc.Layers,
            .samples = desc.Samples,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = desc.Usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo alloc = {
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };

        VK_CHECK(vmaCreateImage(m_allocator, &info, &alloc, &image, &allocation, nullptr));
    }

    /* Create image view */
    VkImageView imageView;
    {
        VkImageViewCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = desc.ViewType,
            .format = desc.Format,
            .subresourceRange = subresRange,
        };

        VK_CHECK(vkCreateImageView(m_device, &info, nullptr, &imageView));
    }

    return Image(allocation, image, imageView, desc, subresRange);
}


void RenderDevice::DestroyImage(const Image& image)
{
    vkDestroyImageView(m_device, image.GetView(), nullptr);

    if (VmaAllocation allocation = image.GetAllocation())
    {
        vmaDestroyImage(m_allocator, image, allocation);
    }
}


void RenderDevice::Submit(std::function<void(CommandBuffer)> callback, const SubmitInfo& submit)
{
    CommandBuffer cmdBuffer = m_transferCmdBuffer;

    /* Wait until transferCmdBuffer is not pending anymore */
    vkWaitForFences(m_device, 1, &m_transferPending, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_transferPending);

    cmdBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    callback(cmdBuffer);
    cmdBuffer.End();

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = submit.Wait ? 1u : 0u,
        .pWaitSemaphores = submit.Wait ? &submit.Wait : nullptr,
        .pWaitDstStageMask = submit.Wait ? &submit.WaitStage : nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = cmdBuffer.Ptr(),
        .signalSemaphoreCount = submit.Signal ? 1u : 0u,
        .pSignalSemaphores = submit.Signal ? &submit.Signal : nullptr,
    };

    VK_CHECK(vkQueueSubmit(m_transferQueue, 1, &submitInfo, m_transferPending));
}


void RenderDevice::UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout, VkImageSubresourceLayers subresLayers)
{
    const auto subresRange = dstImage->GetSubresourceRange();
    ASSERT(subresLayers.aspectMask & subresRange.aspectMask != 0);
    ASSERT(subresLayers.mipLevel < subresRange.levelCount);
    ASSERT(subresLayers.baseArrayLayer >= subresRange.baseArrayLayer);
    ASSERT(subresLayers.baseArrayLayer + subresLayers.layerCount <= subresRange.layerCount);


    Buffer stagingBuffer = CreateBuffer({
        .Size = dstImage->GetBufferSize(),
        .Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .AllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    });
    this->Write(&stagingBuffer, 0, srcData, stagingBuffer.GetSize());

    this->Submit([&](CommandBuffer cmdBuffer)
    {
        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        }, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy dstRegion = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .imageSubresource = subresLayers,
            .imageOffset = { 0, 0, 0 },
            .imageExtent = dstImage->GetSize(),
        };
        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, *dstImage, dstImage->GetCurrentLayout(), 1, &dstRegion);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        }, dstImage, newLayout);
    });
}


void RenderDevice::UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout)
{
    VkImageSubresourceLayers subresLayers = {
        .aspectMask = dstImage->GetAspect(),
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    return UploadImage(dstImage, srcData, newLayout, subresLayers);
}


void RenderDevice::UploadBuffer(Buffer *dstBuffer, size_t dstOffset, const void *srcData, size_t size)
{
    ASSERT(dstOffset + size <= dstBuffer->GetSize());

    Buffer stagingBuffer = CreateBuffer({
        .Size = size,
        .Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .AllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    });
    this->Write(&stagingBuffer, 0, srcData, stagingBuffer.GetSize());

    this->Submit([&](CommandBuffer cmdBuffer)
    {
        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,            
        }, dstBuffer, dstOffset, size);

        cmdBuffer.Copy(&stagingBuffer, dstBuffer, {
            .srcOffset = 0,
            .dstOffset = dstOffset,
            .size = size,
        });

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
        }, dstBuffer, dstOffset, size);        
    });
}