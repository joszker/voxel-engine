#pragma once
#include "Base/Base.h"
#include "Base/Log.h"

#include <vulkan/vulkan.h>



#define VK_CHECK(X) do { if (X != VK_SUCCESS) { std::string_view call = #X; LOG_ERROR("Vulkan-API call {} has failed", call.substr(0, call.find('('))); ASSERT(0); } } while (0)

#if DEBUG_MODE
#   define VK_DEBUG_CHECK(X)    VK_CHECK(X)
#else
#   define VK_DEBUG_CHECK(X)    X
#endif



namespace VkUtils {

inline uint32_t GetTexelSize(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
            return 1;

        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
            return 2;

        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8_SINT:
            return 3;

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return 4;

        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SFLOAT:
            return 2;

        case VK_FORMAT_R16G16_SFLOAT:
            return 4;

        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;

        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32_UINT:
            return 4;

        case VK_FORMAT_R32G32_SFLOAT:
            return 8;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;

        default:
            return 0; // unknown or compressed
    }
}


#if 0
inline void MakeImageRenderable(VkCommandBuffer cmdBuffer, VkImage image)
{
    const VkImageMemoryBarrier imageMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // srcStageMask
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // dstStageMask
        0,                                              // dependencyFlags
        0, nullptr,                                     // memory barriers
        0, nullptr,                                     // buffer memory barriers
        1, &imageMemoryBarrier                          // image memory barriers
    );
}


inline void MakeImagePresentable(VkCommandBuffer cmdBuffer, VkImage image)
{
    const VkImageMemoryBarrier imageMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,           // dstStageMask
        0,                                              // dependencyFlags
        0, nullptr,                                     // memory barriers
        0, nullptr,                                     // buffer memory barriers
        1, &imageMemoryBarrier                          // image memory barriers
    );
}
#endif

}