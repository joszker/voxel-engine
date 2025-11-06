#pragma once
#include "Renderer/CommandBuffer.h"


#include <vector>
#include <memory>

class SwapChain;
class RenderDevice;
class Application;

class Scene
{
public:
    Scene() {}
    virtual ~Scene() {}

    virtual void OnAttach() = 0;
    virtual void OnDetach() = 0;

    virtual void OnUpdate(f32 dt) = 0;
    virtual void OnRender(Image& image, CommandBuffer& cmdBuffer) = 0;

    virtual void OnResizeEvent(int width, int height) {}
    virtual bool OnKeyEvent(int key, int scancode, int action) { return false; }
    virtual bool OnMouseButtonEvent(int button, int action) { return false; }

    void Attach(Application* app);
    void Push(std::shared_ptr<Scene> scene);
    void Transition(std::shared_ptr<Scene> scene);

protected:
    Application *m_app;
    RenderDevice* m_device;
    SwapChain* m_swapChain;
};