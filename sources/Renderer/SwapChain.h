#pragma once
#include "RenderDevice.h"
#include "Image.h"
#include "CommandBuffer.h"



class SwapChain
{
public:
    SwapChain(RenderDevice& device);

    void Init(struct GLFWwindow* window, uint32_t maxFramesInFlight);
    void Recreate();

    Image* AcquireImage(CommandBuffer *cmdBuffer);
    void Submit(VkCommandBuffer cmds);
    void Present();


    inline operator VkSwapchainKHR() const { return m_handle; }
    inline VkFormat GetImageFormat() const { return m_format; }
    inline VkExtent2D GetExtent() const { return m_extent; }
    inline VkExtent3D GetSize() const { return { m_extent.width, m_extent.height, 1 }; }
    inline VkRect2D GetArea() const { return VkRect2D{ .offset={}, .extent=m_extent }; }
    inline size_t GetImageCount() const { return m_images.size(); }
    inline VkImage GetImage(size_t index) const { return m_images[index]; }
    inline VkImageView GetImageView(size_t index) const { return m_images[index].GetView(); }

    inline uint32_t GetFrameCount() const { return (uint32_t)m_frames.size(); }
    inline uint32_t GetCurrentImageIndex() const { return m_imageIndex; }
    inline uint32_t GetCurrentFrameIndex() const { return m_frameIndex; }

private:
    RenderDevice& m_device;
    struct GLFWwindow* m_window{};
    VkSwapchainKHR m_handle{};
    VkFormat m_format{};
    VkExtent2D m_extent{};
    std::vector<Image> m_images;
    struct Frame
    {
        VkSemaphore imageAvailable;
        VkSemaphore renderFinished;
        VkFence inFlight;
        CommandBuffer cmdBuffer;
    };
    std::vector<Frame> m_frames;
    uint32_t m_frameIndex;
    uint32_t m_imageIndex;
};
