#include "Application.h"
#include "Scene.h"
#include "Base/Log.h"

#include <GLFW/glfw3.h>


Application::Application() : m_window(nullptr)
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
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    if (!(m_window = glfwCreateWindow(1280, 720, "VxDemo", nullptr, nullptr)))
        return false;

    Scene scene;
    scene.Init(m_window);

    while (!glfwWindowShouldClose(m_window)) 
    {
        glfwPollEvents();

        scene.Render();
    }

    return true;
}
