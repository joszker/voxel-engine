#include "Base/Base.h"
#include "VkBase.h"



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


    inline VkInstance GetInstance() const { return m_instance; }
    inline operator VkDevice() const { return m_device; }

    inline VkPhysicalDevice GetAdapter() const { return m_adapter; }
    inline uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    inline VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    inline VkQueue GetPresentQueue() const { return m_presentQueue; }

    inline VkSwapchainKHR GetSwapChain() const { return m_swapChain; }
    inline VkFormat GetSwapChainImageFormat() const { return m_swapImageFormat; }
    inline VkExtent2D GetSwapChainExtent() const { return m_swapChainExtent; }
    inline VkRect2D GetSwapChainArea() const { return VkRect2D{ .offset={}, .extent=m_swapChainExtent }; }
    inline size_t GetSwapChainImageCount() const { return m_swapImages.size(); }
    inline std::vector<VkImage> GetSwapChainImages() const { return m_swapImages; }
    inline std::vector<VkImageView> GetSwapChainImageViews() const { return m_swapImageViews; }


private:
    static VkBool32 debugMessageHandler(
        VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
        VkDebugUtilsMessageTypeFlagsEXT             types,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData
    ); 


private:
    VkInstance m_instance{};
    VkSurfaceKHR m_surface{};
    VkPhysicalDevice m_adapter{};
    VkPhysicalDeviceFeatures m_adapterFeatures{};
    VkDevice m_device{};
    uint32_t m_presentQueueFamily;
    uint32_t m_graphicsQueueFamily;
    uint32_t m_computeQueueFamily;
    uint32_t m_transferQueueFamily;
    VkQueue m_presentQueue{};
    VkQueue m_graphicsQueue{};
    VkSwapchainKHR m_swapChain{};
    VkFormat m_swapImageFormat;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
};