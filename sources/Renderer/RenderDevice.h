#pragma once
#include "Base/Base.h"
#include "Image.h"
#include "Buffer.h"


class CommandBuffer;


struct SubmitInfo
{
    /* Semaphores upon which to wait before executing */
    VkSemaphore Wait{};

    /* Stage at which to wait */
    VkPipelineStageFlags WaitStage{};

    /* Semaphore to be signaled when execution is completed */
    VkSemaphore Signal{};

    /* Fence to be signaled when execution is completed */
    //VkFence SignalFence{};
};


/*
    Vulkan device abstraction. Handles
*/
class RenderDevice
{
public:
    RenderDevice() = default;

    bool Init(struct GLFWwindow* window, bool debug);

    VkShaderModule CreateShaderModule(std::span<const byte> code) const;
    VkSemaphore MakeSemaphore() const;
    VkFence MakeFence(bool signaled=false) const;
    void DestroySemaphore(VkSemaphore semaphore) const;
    
    Image CreateImage(const VkSwapchainCreateInfoKHR& scInfo, VkImage scImage);
    Image CreateImage(const ImageDesc& desc);
    Image CreateImage(ImageDesc desc, const VkImageSubresourceRange& subresRange);
    void DestroyImage(const Image& image);

    Buffer CreateBuffer(const BufferDesc& desc);
    void DestroyBuffer(const Buffer& buffer);

    /* Copies `src` data to mapped buffer */
    void Write(Buffer *buffer, size_t dstOffset, const void *src, size_t numBytes);

    /* Submit commands to transfer queue */
    void Submit(std::function<void(CommandBuffer)> callback, const SubmitInfo& submit = {});
    /* Submits image upload to transfer queue */
    void UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout, VkImageSubresourceLayers subresLayers);
    /* Submits image upload to transfer queue */    
    void UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout);
    /* Submits buffer upload to transfer queue */    
    void UploadBuffer(Buffer *dstBuffer, size_t dstOffset, const void *srcData, size_t size);


    inline VkInstance GetInstance() const { return m_instance; }
    inline VkSurfaceKHR GetSurface() const { return m_surface; }
    inline operator VkDevice() const { return m_device; }
    inline VkPhysicalDevice GetAdapter() const { return m_adapter; }
    inline uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    inline uint32_t GetPresentQueueFamily() const { return m_presentQueueFamily; }
    inline VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    inline VkQueue GetPresentQueue() const { return m_presentQueue; }
    inline VkCommandPool GetGraphicsCommandPool() const { return m_graphicsCmdPool; }
    inline std::string GetDebugDeviceName() { return m_debug.deviceName; }

private:
    static VkBool32 debugMessageHandler(
        VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
        VkDebugUtilsMessageTypeFlagsEXT             types,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData
    ); 

    bool hasDedicatedTransferQueue() { return m_graphicsQueue != m_transferQueue; }


private:
    VkInstance m_instance{};
    VkSurfaceKHR m_surface{};
    VkPhysicalDevice m_adapter{};
    VkPhysicalDeviceFeatures m_adapterFeatures{};
    VkDevice m_device{};
    uint32_t m_presentQueueFamily;
    uint32_t m_graphicsQueueFamily;
    uint32_t m_transferQueueFamily;
    VkQueue m_presentQueue{};
    VkQueue m_graphicsQueue{};
    VkQueue m_transferQueue{};          // NOTE: can be same as `m_graphicsQueue`

    VmaAllocator m_allocator;
    VkCommandPool m_graphicsCmdPool;
    VkCommandPool m_transferCmdPool;    // NOTE: can be same as `m_graphicsCmdPool`
    VkCommandBuffer m_transferCmdBuffer;
    VkFence m_transferPending;

    struct
    {
        std::string deviceName;
    } m_debug;
};