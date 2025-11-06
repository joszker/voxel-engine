#pragma once
#include "Renderer/RenderDevice.h"
#include "Renderer/SwapChain.h"

#include <memory>


class Scene;


class Application
{
    using Window = struct GLFWwindow*;
public:
    Application();
    ~Application();

    bool Run();

    Window GetWindow() const;
    RenderDevice& GetDevice();
    SwapChain& GetSwapChain();

    void PushScene(std::shared_ptr<Scene> scene);
    void Transition(std::shared_ptr<Scene> scene);

private:
    static void onResizeEvent(Window window, int width, int height);
    static void onKeyEvent(Window window, int key, int scancode, int action, int mods);
    static void onMouseButtonEvent(Window window, int button, int action, int mods);

    Scene* getCurrentScene();

private:
    Window m_window;
    RenderDevice m_device;
    SwapChain m_swapChain;
    std::vector<std::shared_ptr<Scene>> m_scenes;
};
