#pragma once
#include <vulkan/vulkan.h>



#define VK_CHECK(X) do { if (X != VK_SUCCESS) { std::string_view call = #X; LOG_ERROR("Vulkan-API call {} has failed", call.substr(call.find('(') + 1)); ASSERT(0); } } while (0)


 