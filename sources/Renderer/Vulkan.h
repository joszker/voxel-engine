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

}