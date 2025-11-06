#include "Application.h"
#include "Base/Log.h"
#include "Scene.h"
#include "MainScene.h"

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
    }


    m_device.Init(m_window, true);
    m_swapChain.Init(m_window, 2);

    PushScene(std::make_shared<MainScene>());

    while (!glfwWindowShouldClose(m_window)) 
    {
        glfwPollEvents();

        //imguiLayer->BeginNewFrame();

        if (Scene* currentScene = getCurrentScene())
        {
            currentScene->OnUpdate(0.0f);

            CommandBuffer cmdBuffer;
            Image* image = m_swapChain.AcquireImage(&cmdBuffer);

            cmdBuffer.Begin();
            currentScene->OnRender(*image, cmdBuffer);
            MakeImagePresentable(cmdBuffer, *image);
            cmdBuffer.End();

            m_swapChain.Submit(cmdBuffer);
        }

        m_swapChain.Present();
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


void Application::onResizeEvent(Window window, int width, int height)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (Scene* currentScene = app->getCurrentScene())
    {
        currentScene->OnResizeEvent(width, height);
    }
}


void Application::onKeyEvent(Window window, int key, int scancode, int action, int mods)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

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


Scene* Application::getCurrentScene()
{
    if (!m_scenes.empty())
    {
        return m_scenes.back().get();
    }
    return nullptr;
}