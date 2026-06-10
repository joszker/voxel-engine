#pragma once
#include "Scene.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Voxel/SVTree.h"


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


private:
    void createImages(int width, int height);
    VkShaderModule loadShader(std::string_view path);

    void initImGui();
    void deinitImGui();

private:
    Pipeline m_primaryPass{};
    Pipeline m_denoisingPass{};
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


    struct Frame
    {
        Buffer perFrameData;    // Uniform buffer written every frame
        VkDescriptorSet descriptorSet{};

        //Image primaryImage;

        Image voxelImage;
        Image normalImage;
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
    } m_camera{};


    uint m_fps;
    uint m_frameCounter;
    f64 m_prevFpsTime;
    uint m_frameNum = 0;
};