#pragma once
#include "Scene.h"


class Application
{
public:
    Application();
    ~Application();

    bool Run();

private:
    static void onWindowResize(GLFWwindow *window, int width, int height);

private:
    struct GLFWwindow *m_window;
    Scene m_scene;
};