#pragma once


#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <stdio.h>


int main(void)
{
    uint32_t numLayers = 0;
    vkEnumerateInstanceLayerProperties(&numLayers, nullptr);

    printf(">%d\n", numLayers);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Demo", nullptr, nullptr);

    while (!glfwWindowShouldClose(window)) 
    {
        glfwPollEvents();
    }
}