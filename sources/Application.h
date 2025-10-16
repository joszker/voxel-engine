#pragma once


class Application
{
public:
    Application();
    ~Application();

    bool Run();

private:
    struct GLFWwindow *m_window;
};