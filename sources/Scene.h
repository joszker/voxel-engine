#pragma once
#include "Renderer/RenderDevice.h"


class Scene
{
public:
    Scene();

    void Init(struct GLFWwindow *window);
    void Render();

private:
    void recordCommandBuffer(size_t imageIndex);

private:
    const size_t k_maxFramesInFlight = 2;

    RenderDevice m_device;
    VkPipeline m_pipeline{};
    VkCommandPool m_cmdPool{};
    std::vector<VkCommandBuffer> m_cmdBuffers;
    std::vector<VkSemaphore> m_imageAvailable;    // Signaled on the GPU once acquired swap chain image becomes available.
    std::vector<VkSemaphore> m_renderFinished;    // Signaled on the GPU once submitted commands have finished executing.
    std::vector<VkFence> m_inFlight;              // Signaled on the CPU once submitted commands have finished executing.

    size_t m_frameIndex = 0;
};