#pragma once
#include "Base/Base.h"

#include <memory>

class Image;
class CommandBuffer;
class SwapChain;
class RenderDevice;
class Application;

/*
    Scene base class.
*/
class Scene
{
public:
    Scene() {}
    virtual ~Scene() {}

    /* Called whenever the scene is attached */
    virtual void OnAttach() = 0;

    /* Called whenever the scene is detached */ 
    virtual void OnDetach() = 0;

    /* Called whenever the scene is being updated */     
    virtual void OnUpdate(f32 dt) = 0;

    /* Called whenever the scene is being rendered */     
    virtual void OnRender(Image& image, CommandBuffer& cmdBuffer, f32 alpha) = 0;

    /* Called whenever the window resizes */     
    virtual void OnResizeEvent(int width, int height) {}

    /* Called whenever a key has been pressed/released */
    virtual bool OnKeyEvent(int key, int scancode, int action) { return false; }

    /* Called whenever a mouse button has been pressed/released */
    virtual bool OnMouseButtonEvent(int button, int action) { return false; }

    /* Called whenever the mouse has been moved */
    virtual void OnMouseMovedEvent(Vec2 pos) {}

    /* Called whenever the mouse has been scrolled */
    virtual void OnMouseScrolledEvent(Vec2 offset) {}

    void Attach(Application* app);

    /* Pushes the desired scene to top */
    void Push(std::shared_ptr<Scene> scene);

    /* Transitions the current scene to the desired scene */    
    void Transition(std::shared_ptr<Scene> scene);

protected:
    bool isKeyDown(int key);
    bool isMouseButtonDown(int button);
    Vec2 getCursorPosition();

protected:
    Application *m_app;
    RenderDevice* m_device;
    SwapChain* m_swapChain;
};