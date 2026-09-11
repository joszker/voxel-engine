#pragma once
#include "Scene.h"


class MainScene;


/*
    Main menu scene from which maps can be selected.
*/
class MenuScene : public Scene
{
public:
    MenuScene();
    ~MenuScene() override {}

    void OnAttach() override;
    void OnDetach() override;

    void OnUpdate(f32 dt) override;
    void OnRender(Image& image, CommandBuffer& cmdBuffer, f32 alpha) override;

    void OnResizeEvent(int width, int height) override {}
    bool OnKeyEvent(int key, int scancode, int action) override;
    void OnMouseMovedEvent(Vec2 pos) override {}
    bool OnMouseButtonEvent(int button, int action) override { return false; }

private:
    void play();
    void addMap(const char *map);

private:
    std::vector<const char*> m_maps;
    int m_mapIndex = 0;

    std::vector<std::u8string> m_recordings;
    int m_recordingIndex = 0;

    std::shared_ptr<MainScene> m_mainScene;
    bool m_enableSpecular = false;
};
