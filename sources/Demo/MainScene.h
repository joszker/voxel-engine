#pragma once
#include "Scene.h"
#include "Voxel/VoxelRenderer.h"

#include <atomic>
#include <memory>
#include <future>
#include <queue>



/*
    Main scene which implements all user interactions with the voxel world.
*/
class MainScene : public Scene
{
public:
    MainScene(std::string mapName, std::string recName, bool specular);
    ~MainScene() override {}

    bool IsReady();

    void OnAttach() override;
    void OnDetach() override;

    void OnUpdate(f32 dt) override;
    void OnRender(Image& image, CommandBuffer& cmdBuffer, f32 alpha) override;

    void OnResizeEvent(int width, int height) override;
    bool OnKeyEvent(int key, int scancode, int action) override;
    void OnMouseMovedEvent(Vec2 pos) override;
    bool OnMouseButtonEvent(int button, int action) override;
    void OnMouseScrolledEvent(Vec2 offset) override;

private:
    void handleMouseClick(bool buttonL, bool buttonR);
    void renderGui();
    void materialEditor();
    void objectPicker();

    void loadMap(std::string path);
    void loadModel(std::string path, VoxelArray* model);
    void addObject(std::string name);

    void saveRecordedInput();
    bool loadRecordedInput(std::string path);
    void saveFpsHistory();

private:
    std::future<void> m_svtLoader;
    std::atomic<bool> m_svtIsReady;
    SVTree m_svt;
    bool m_svtModified = false;
    bool m_enableSpecular;
    std::string m_mapName;

    VoxelRenderer m_renderer;

    struct Camera
    {
        bool active;
        Vec3 pos;
        Vec3 dir;
        f32 yaw;
        f32 pitch;
        f32 fov;
    } m_camera{};

    bool m_showInfo = true;

    u32 m_frameNum = 0;
    f64 m_fpsPrevTime;
    u32 m_fpsCounter = 0;
    u32 m_fps = 0;
    std::vector<u16> m_fpsHistory;
    
    VoxelPalette m_palette;
    uint m_selectedMaterial = 255;

    VoxelRenderParams m_renderParams;

    Vec2 m_prevCursorPos{};
    bool m_mouseButtonPressed = false;
    f32 m_mouseTimer = 0.0f;
    u32 m_gen;

    struct VoxelObject
    {
        std::atomic<bool> Loaded;
        std::string Name;
        VoxelArray Voxels;

        VoxelObject() = default;
        VoxelObject(std::string name) : Name(name) {}

        bool IsLoaded() { return Loaded.load(); }
    };
    std::vector<std::unique_ptr<VoxelObject>> m_objects;
    VoxelObject *m_selectedObject = nullptr;
    std::future<void> m_objectLoader;
    std::atomic<bool> m_objectIsLoading;
    int m_brushSize = 5;

    struct Input
    {
        Vec3 position;
        Vec3 direction;        
        bool mouseButtonL;
        bool mouseButtonR;
    } m_input{};
    std::vector<Input> m_inputQueue;

    std::string m_inputRecName;
    size_t m_inputReplayIndex = 0;
    f64 m_inputStartTime = 0.0f;
    bool m_recordingInput = false;
    bool m_playingInput = false;
};