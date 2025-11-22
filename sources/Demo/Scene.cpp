#include "Scene.h"
#include "Application.h"

#include <GLFW/glfw3.h>


void Scene::Attach(Application* app)
{
    m_app = app;
    m_device = &app->GetDevice();
    m_swapChain = &app->GetSwapChain();
    
    this->OnAttach();
}


void Scene::Push(std::shared_ptr<Scene> scene)
{
    m_app->PushScene(scene);
}


void Scene::Transition(std::shared_ptr<Scene> scene)
{
    m_app->Transition(scene);
}


Vec2 Scene::getCursorPosition()
{
    double x, y;
    glfwGetCursorPos(m_app->GetWindow(), &x, &y);
    return { x, y };
}