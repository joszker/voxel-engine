#pragma once
#include "Scene.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Voxel/SVTree.h"

#include <atomic>
#include <memory>
#include <future>


struct alignas(16) Material
{
    Vec4u8 color;
    float emission;
};



class MainScene : public Scene
{
public:
    MainScene() {}
    ~MainScene() override {}

    void OnAttach() override;
    void OnDetach() override;

    void OnUpdate(f32 dt) override;
    void OnRender(Image& image, CommandBuffer& cmdBuffer) override;

    void OnResizeEvent(int width, int height) override;
    bool OnKeyEvent(int key, int scancode, int action) override;
    void OnMouseMovedEvent(Vec2 pos) override;
    bool OnMouseButtonEvent(int button, int action) override;

private:
    void loadMap(std::string path);
    void loadModel(std::string path, VoxelArray* model);

    void writeSVT(Buffer *svtBuffer);

    void createImages(int width, int height);
    VkShaderModule loadShader(std::string_view path);

    void updatePalette();

    void initImGui();
    void deinitImGui();

    void handleMouseClick(bool buttonL, bool buttonR);

private:
    Pipeline m_primaryPass{};
    Pipeline m_blurPass{};
    Pipeline m_averagingPass{};
    Pipeline m_purgePip;
    Pipeline m_compositePass{};

    VkSampler m_sampler{};
    VkDescriptorPool m_descriptorPool{};

    SVTree m_svt;

    struct
    {
        Buffer gpu;     // gpu local buffer
        Buffer cpu;     // cpu staging buffer
    } m_svtBuffer;

    Image m_stbnImage;

    Buffer m_vhmBuffer;
    VkDeviceAddress m_prevVHM;
    VkDeviceAddress m_currentVHM;

    Buffer m_paletteBuffer;

    struct Frame
    {
        Buffer perFrameData;    // Uniform buffer written every frame
        VkDescriptorSet descriptorSet{};

        //Image primaryImage;

        Image voxelImage;
        Image normalImage;
        Image radianceImage;
        //Image blurImage;
        Image specularSunImage;
    };
    std::vector<Frame> m_frames;

    Vec2 m_prevCursorPos{};

    struct
    {
        bool active;
        Vec3 pos;
        Vec3 dir;
        f32 yaw;
        f32 pitch;
        Mat4 projection;
    } m_camera{};


    f64 m_prevTime;
    u32 m_frameNum = 0;
    u32 m_fps = 0;
    std::array<Material, 256> m_palette{};
    uint m_selectedMaterial = 255;
    bool m_paletteChanged = false;

    Vec3 m_ambientLight;
    Vec3 m_sunLight;
    float m_sunElevation;
    float m_sunAzimuth;
    float m_sunConeAngle;
    Vec3 m_skyColor;

    bool m_svtModified = false;
    bool m_svtLoad = false;

    bool m_mouseButtonPressed = false;
    f32 m_mouseTimer = 0.0f;
    bool m_purge = false;
    u32 m_gen;

    struct VoxelObject
    {
        std::atomic<bool> Loaded;   // TODO: atomic<int> Status
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
    bool m_editMode = true;
};