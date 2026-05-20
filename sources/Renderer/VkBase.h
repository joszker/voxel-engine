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


#if 1
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