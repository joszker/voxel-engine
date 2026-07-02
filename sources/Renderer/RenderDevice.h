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


class RenderDevice
{
public:
    RenderDevice() = default;

    bool Init(struct GLFWwindow* window, bool debug);

    void ResizeSwapChain(struct GLFWwindow* window);
    void ResizeSwapChain(uint32_t width, uint32_t height);


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
    void Write(Buffer *buffer, size_t dstOffset, const void *src, size_t numBytes);

    void Submit(std::function<void(CommandBuffer)> callback, const SubmitInfo& submit = {});
    void UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout, VkImageSubresourceLayers subresLayers);
    void UploadImage(Image *dstImage, const void *srcData, VkImageLayout newLayout);
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

    #if 0
    inline VkSwapchainKHR GetSwapChain() const { return m_swapChain; }
    inline VkFormat GetSwapChainImageFormat() const { return m_swapImageFormat; }
    inline VkExtent2D GetSwapChainExtent() const { return m_swapChainExtent; }
    inline VkRect2D GetSwapChainArea() const { return VkRect2D{ .offset={}, .extent=m_swapChainExtent }; }
    inline size_t GetSwapChainImageCount() const { return m_swapImages.size(); }
    inline std::vector<VkImage> GetSwapChainImages() const { return m_swapImages; }
    inline std::vector<VkImageView> GetSwapChainImageViews() const { return m_swapImageViews; }
    #endif


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
    
    #if 0
    VkSwapchainKHR m_swapChain{};
    VkFormat m_swapImageFormat;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    #endif
};