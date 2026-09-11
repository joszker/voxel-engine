#include "Application.h"
#include "Base/Log.h"
#include "Scene.h"
#include "MainScene.h"
#include "MenuScene.h"
#include "ImGuiLayer.h"

#include <GLFW/glfw3.h>


Application::Application() : m_window(nullptr), m_swapChain(m_device)
{

}


Application::~Application()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}


bool Application::Run()
{
    glfwSetErrorCallback([](int code, const char *description)
    {
        /* Called whenever a GLFW error occurs */
        LOG_ERROR("[GLFW] \"{}\" ({})", description, code);
    });

    /* Setup GLFW context and window */
    {
        if (!glfwInit())
            return false;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        if (!(m_window = glfwCreateWindow(1280, 720, "VxDemo", nullptr, nullptr)))
            return false;


        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, onResizeEvent);
        glfwSetKeyCallback(m_window, onKeyEvent);
        glfwSetMouseButtonCallback(m_window, onMouseButtonEvent);
        glfwSetCursorPosCallback(m_window, onMouseMovedEvent);
        glfwSetScrollCallback(m_window, onScrollEvent);
    }

    const u64 timerFreq = glfwGetTimerFrequency();
    u64 previousTime = glfwGetTimerValue();

#if _DEBUG
    m_device.Init(m_window, true);
#else
    m_device.Init(m_window, false);
#endif
 //   m_swapChain.Init(m_window, 1, true);    // 1 FIF + vsync
    m_swapChain.Init(m_window, 1, false);   // 1 FIF

 //   m_swapChain.Init(m_window, 2, true);  // 2 FIF + vsync
//    m_swapChain.Init(m_window, 2, false); // 2 FIF

    ImGuiLayer::Init(*this);

    PushScene(std::make_shared<MenuScene>());

    constexpr f64 updatesPerSec = 60.0;
    constexpr f64 deltaTime = 1.0 / updatesPerSec;
    

    while (!glfwWindowShouldClose(m_window)) 
    {
        glfwPollEvents();

        u64 currentTime = glfwGetTimerValue();
        f64 frameTime = f32(currentTime - previousTime) / f32(timerFreq);
        frameTime = glm::min(frameTime, 0.25);
        previousTime = currentTime;

        if (Scene* currentScene = getCurrentScene())
            currentScene->OnUpdate(f32(frameTime));

        CommandBuffer cmdBuffer;
        Image* image = m_swapChain.AcquireImage(&cmdBuffer);
        if (image)
        {
            cmdBuffer.Begin();

            if (Scene* currentScene = getCurrentScene())
            {
                currentScene->OnRender(*image, cmdBuffer, 0.0f);
            }

            cmdBuffer.Barrier({
                .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .NextStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            }, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            cmdBuffer.End();

            m_swapChain.Submit(cmdBuffer);

            m_swapChain.Present();
        }
    }

    return true;
}


GLFWwindow* Application::GetWindow() const
{
    return m_window;
}


RenderDevice& Application::GetDevice()
{
    return m_device;
}


SwapChain& Application::GetSwapChain()
{
    return m_swapChain;
}


void Application::PushScene(std::shared_ptr<Scene> scene)
{
    scene->Attach(this);
    m_scenes.push_back(scene);
}


void Application::Transition(std::shared_ptr<Scene> scene)
{
    auto& previous = m_scenes.back();
    previous->OnDetach();
    scene->Attach(this);
    previous.swap(scene);
}


void Application::SetCursorEnabled(bool enabled)
{
    glfwSetInputMode(m_window, GLFW_CURSOR, enabled ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}


void Application::onResizeEvent(Window window, int width, int height)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    app->m_swapChain.Recreate();

    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnResizeEvent(width, height);
    }
}


void Application::onKeyEvent(Window window, int key, int scancode, int action, int mods)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
    {
        bool isFullscreen = glfwGetWindowMonitor(window);
        bool hasBorders = glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_TRUE;

        if (isFullscreen)
        {
            // Restore windowed mode:
            glfwSetWindowMonitor(
                app->m_window, nullptr, 
                app->m_windowPos.x, app->m_windowPos.y, app->m_windowSize.x, app->m_windowSize.y,
                GLFW_DONT_CARE
            );
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        }
        else if (GLFWmonitor *monitor = glfwGetPrimaryMonitor())
        {
            if (hasBorders)
            {
                // Save window placement:
                glfwGetWindowPos(app->m_window, &app->m_windowPos.x, &app->m_windowPos.y);
                glfwGetWindowSize(app->m_window, &app->m_windowSize.x, &app->m_windowSize.y);
            }

            int monitorX, monitorY, monitorW, monitorH;
            glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorW, &monitorH);

            glfwSetWindowMonitor(app->m_window, monitor, monitorX, monitorY, monitorW, monitorH, GLFW_DONT_CARE);
        }
        return;
    }
    else if (key == GLFW_KEY_F10 && action == GLFW_PRESS)
    {
        int monitorX, monitorY, monitorW, monitorH;
        glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &monitorX, &monitorY, &monitorW, &monitorH);

        bool isFullscreen = glfwGetWindowMonitor(window);
        bool hasBorders = glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_TRUE;

        if (isFullscreen)
        {
            glfwSetWindowMonitor(app->m_window, nullptr, monitorX, monitorY, monitorW, monitorH, GLFW_DONT_CARE);
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        }
        else if (!isFullscreen && hasBorders)
        {
            // Save window placement:
            glfwGetWindowPos(app->m_window, &app->m_windowPos.x, &app->m_windowPos.y);
            glfwGetWindowSize(app->m_window, &app->m_windowSize.x, &app->m_windowSize.y);

            // Set to work area:
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowPos(window, monitorX, monitorY);
            glfwSetWindowSize(window, monitorW, monitorH);
        } 
        else
        {
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);

            // Restore window placement:
            glfwSetWindowPos(window, app->m_windowPos.x, app->m_windowPos.y);
            glfwSetWindowSize(window, app->m_windowSize.x, app->m_windowSize.y);
        }
    }
    else if (key == GLFW_KEY_F9 && action == GLFW_PRESS)
    {
        glfwSetWindowSize(window, 1920, 1080);
    }


    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnKeyEvent(key, scancode, action);
    }
}


void Application::onMouseButtonEvent(Window window, int button, int action, int mods)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnMouseButtonEvent(button, action);
    }
}


void Application::onMouseMovedEvent(Window window, double xPos, double yPos)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnMouseMovedEvent(Vec2(float(xPos), float(yPos)));
    }    
}


void Application::onScrollEvent(Window window, double xOffset, double yOffset)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnMouseScrolledEvent(Vec2(float(xOffset), float(yOffset)));
    }    
}


Scene* Application::getCurrentScene()
{
    if (!m_scenes.empty())
    {
        return m_scenes.back().get();
    }
    return nullptr;
}