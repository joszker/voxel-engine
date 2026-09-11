#pragma once
#include "Renderer/RenderDevice.h"
#include "Renderer/SwapChain.h"

#include <memory>


class Scene;


/*
    Main application class.
*/
class Application
{
    using Window = struct GLFWwindow*;
public:
    Application();
    ~Application();

    /* Initializes the application and runs the main loop */
    bool Run();

    Window GetWindow() const;
    RenderDevice& GetDevice();
    SwapChain& GetSwapChain();

    /* See Scene::PushScene */
    void PushScene(std::shared_ptr<Scene> scene);

    /* See Scene::Transition */
    void Transition(std::shared_ptr<Scene> scene);

    /* Enables/disables the mouse cursor */
    void SetCursorEnabled(bool enabled);

private:
    static void onResizeEvent(Window window, int width, int height);
    static void onKeyEvent(Window window, int key, int scancode, int action, int mods);
    static void onMouseButtonEvent(Window window, int button, int action, int mods);
    static void onMouseMovedEvent(Window window, double xPos, double yPos);
    static void onScrollEvent(Window window, double xOffset, double yOffset);

    Scene* getCurrentScene();

private:
    Window m_window;
    RenderDevice m_device;
    SwapChain m_swapChain;
    std::vector<std::shared_ptr<Scene>> m_scenes;

    Vec2i m_windowPos;
    Vec2i m_windowSize;
};
