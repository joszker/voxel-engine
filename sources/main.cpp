#pragma once


#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Base/Base.h"
#include "Base/Log.h"


int main(void)
{
    Log::Init();

    uint32_t numLayers = 0;
    vkEnumerateInstanceLayerProperties(&numLayers, nullptr);

    LOG_TRACE("x = {}", numLayers);
    LOG_INFO("x = {}", numLayers);
    LOG_WARNING("x = {}", numLayers);
    LOG_ERROR("x = {}", 100);
    LOG_FATAL("x = {}", 100);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Demo", nullptr, nullptr);

    while (!glfwWindowShouldClose(window)) 
    {
        glfwPollEvents();
    }
}