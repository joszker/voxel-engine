#pragma once
#include "Vulkan.h"
#include <vk_mem_alloc.h>



struct ImageDesc
{
    VkImageType Type;
    VkFormat Format;
    VkImageUsageFlags Usage;
    VkExtent3D Size;
    uint32_t MipLevels = VK_REMAINING_MIP_LEVELS;
    uint32_t Layers = 1;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageViewType ViewType;
};


class Image
{
public:
    Image() = default;
    Image(VmaAllocation alloc, VkImage image, VkImageView imageView, const ImageDesc& desc, const VkImageSubresourceRange& subresRange);

    inline operator VkImage() const { return m_handle; }
    inline VkImageView GetView() const { return m_view; }
    inline VmaAllocation GetAllocation() const { return m_alloc; }
    inline VkPipelineStageFlags GetCurrentStage() const { return m_currentStage; }
    inline VkImageLayout GetCurrentLayout() const { return m_currentLayout; }

    inline ImageDesc GetDesc() const { return m_desc; }
    inline VkFormat GetFormat() const { return m_desc.Format; }
    inline VkExtent3D GetSize() const { return m_desc.Size; }
    inline VkRect2D GetArea() const {return { { 0, 0 }, { m_desc.Size.width, m_desc.Size.height } }; }
    inline uint32_t GetWidth() const { return m_desc.Size.width; }
    inline uint32_t GetHeight() const { return m_desc.Size.height; }
    inline uint32_t GetDepth() const { return m_desc.Size.depth; }

    inline VkImageAspectFlags GetAspect() const { return m_subresourceRange.aspectMask; }
    inline VkImageSubresourceRange GetSubresourceRange() const { return m_subresourceRange; }

    VkDeviceSize GetBufferSize() const;

private:
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    VkImage m_handle = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkPipelineStageFlags m_currentStage{};
    VkImageLayout m_currentLayout{};
    ImageDesc m_desc{};
    VkImageSubresourceRange m_subresourceRange{};

    friend class CommandBuffer;
    friend class SceneLayer;
};


