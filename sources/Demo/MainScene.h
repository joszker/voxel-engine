#pragma once
#include "Scene.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"


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
    Pipeline m_compositePass{};

    VkSampler m_sampler{};
    VkDescriptorPool m_descriptorPool{};

    struct Frame
    {
        Buffer perFrameData;    // Uniform buffer written every frame
        VkDescriptorSet descriptorSet{};

        Image primaryImage;
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
};