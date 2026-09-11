#include "MainScene.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include "Base/FileSystem.h"
#include "Base/IO.h"
#include "Demo/imgfx.h"

#include "Voxel/VoxLoader.h"
#include "Voxel/VoxelRenderer.h"
#include "Voxel/SVRayCast.h"
#include "Voxel/SVBuilder.h"

#include <fstream>
#include <bitset>

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/bit.hpp>
#include "3rdparty/ogt_vox.h"



#define GAMMA_CORRECT 1


constexpr f64 INPUT_RECORDING_RATE = 30.0;


static float ColorDistance(Vec3 a, Vec3 b)
{
    float r = (a.r + b.r) * 0.5f;

    float dr = a.r - b.r;
    float dg = a.g - b.g;
    float db = a.b - b.b;

    return
        (2.0f + r) * dr * dr +
        4.0f * dg * dg +
        (2.0f + (1.0f - r)) * db * db;
}

static void ColorCorrect(Vec4u8& color)
{
    color.r = u8(powf(float(color.r) / 255.0f, 2.2) * 255.0f);
    color.g = u8(powf(float(color.g) / 255.0f, 2.2) * 255.0f);
    color.b = u8(powf(float(color.b) / 255.0f, 2.2) * 255.0f);
}


static VoxelArray MakeSphere(int radius, Voxel color)
{
    ASSERT(radius > 0);

    VoxelArray sphere;
    sphere.Init(Vec3u(2 * radius - 1));

    const int r2 = radius * radius;

    radius -= 1;
    for (int z = -radius; z <= radius; ++z)
    {
        const int z2 = z * z;

        int x = radius;

        for (int y = 0; y <= radius; ++y)
        {
            while (x * x + y * y + z2 > r2)
                --x;

            // Fill the four symmetric scanlines
            for (int xx = -x; xx <= x; ++xx)
            {
                sphere.Set(Vec3u(xx + radius, y + radius, z + radius), color);

                if (y != 0)
                    sphere.Set(Vec3u(xx + radius, radius - y, z + radius), color);
            }
        }
    }
    return sphere;
}


MainScene::MainScene(std::string mapName, std::string recName, bool specular)
{
    m_svtIsReady.store(false);
    m_svtLoader = std::async([this, mapName]()
    {
        loadMap(std::string("data/") + mapName);
        m_svtIsReady.store(true);
        m_playingInput = true;
    });
    m_inputRecName = recName;
    m_enableSpecular = specular;
    m_mapName = mapName;
}


bool MainScene::IsReady()
{
    return m_svtIsReady.load();
}


void MainScene::OnAttach()
{
    if (!m_inputRecName.empty())
    {
        std::string inputRecPath = "data/recordings/" + m_inputRecName;
        if (!loadRecordedInput(inputRecPath))
            LOG_ERROR("Failed to load recording file '{}'", inputRecPath);
    }

    m_svtLoader.wait();

    m_renderer.Initialize(m_device, m_swapChain, m_enableSpecular);
    m_renderer.UpdateSparseVoxelTree(m_svt);
    m_renderer.UpdatePalette(m_palette);

    m_renderer.SetFinalRenderProc([](CommandBuffer& cmdBuffer)
    {
        // Render at the end of each frame:
        ImGuiLayer::Draw(cmdBuffer);
    });

    /* Init camera */
    {
        m_camera.pos = Vec3(0.0f);
        m_camera.yaw = glm::half_pi<f32>();
        m_camera.dir = {
            cos(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.pitch),
        };
        m_camera.fov = glm::radians(90.0f);
        m_camera.active = true;

        double cursorX, cursorY;
        glfwGetCursorPos(m_app->GetWindow(), &cursorX, &cursorY);
        m_prevCursorPos = Vec2(cursorX, cursorY);
    }


    /* Render parameters */
    {

        m_renderParams.SunElevationAngle = glm::radians(75.0f);
        m_renderParams.SunAzimuthAngle = glm::radians(90.0f);
//        m_renderParams.SunElevationAngle = glm::radians(45.0f);
//        m_renderParams.SunAzimuthAngle = glm::radians(135.0f);
    //    m_sunElevation = glm::radians(50.0f);
    //    m_sunAzimuth = glm::radians(140.0f);
        m_renderParams.SunConeAngle = glm::radians(2.0f);
    //    m_skyColor = { 0.525f, 0.804f, 0.918f };

#if GAMMA_CORRECT
        m_renderParams.AmbientLight = Vec3(0.3);
        m_renderParams.SunLight = Vec3(0.5);    
        m_renderParams.SkyColor = { 0.111f, 0.374f, 0.573f };
#else
        m_renderParams.AmbientLight = Vec3(0.8);
        m_renderParams.SunLight = Vec3(0.5);    
        m_renderParams.SkyColor = { 0.3686274509803922, 0.6392156862745098, 0.7764705882352941 };
#endif

        m_renderParams.AmbientFalloff = 512.0f;
        m_renderParams.AmbientMinimum = 0.01f;
    }
    m_renderer.SetParams(m_renderParams);

    m_app->SetCursorEnabled(false);


    // Objects
    addObject("dragon");
    addObject("teapot");
    addObject("menger");
    addObject("castle");
    addObject("tree");
    addObject("winter_tree");
    addObject("gladius");
    addObject("smallTree");
    addObject("3story");

    ImGfx::SetViewport(float(m_swapChain->GetSize().width), float(m_swapChain->GetSize().height));

    f64 currentTime = glfwGetTime();

    m_fpsPrevTime = currentTime;
    m_inputStartTime = currentTime;
}


void MainScene::OnDetach()
{
    
}


void MainScene::OnUpdate(f32 dt)
{
    /* Measure FPS */
    f64 currentTime = glfwGetTime();
    f64 elapsed = currentTime - m_fpsPrevTime;
    if (elapsed >= 0.5)
    {
        m_fps = f64(m_fpsCounter) / elapsed;
        m_fpsCounter = 0;
        m_fpsPrevTime = currentTime;

        if (m_playingInput)
            m_fpsHistory.push_back(m_fps);
    }

    if (m_playingInput)
    {
        f64 elapsed = currentTime - m_inputStartTime;
        f64 index = elapsed * INPUT_RECORDING_RATE;

        size_t indexA = size_t(index);
        size_t indexB = size_t(index) + 1;

        if (indexB < m_inputQueue.size())
        {
            f32 alpha = glm::fract(index);
            auto& inputA = m_inputQueue[indexA];
            auto& inputB = m_inputQueue[indexB];

            m_camera.pos = glm::mix(inputA.position, inputB.position, alpha);
            m_camera.dir = glm::mix(inputA.direction, inputB.direction, alpha);

            if (indexA >= m_inputReplayIndex)
            {
                m_input = m_inputQueue[indexA];
                m_inputReplayIndex = indexA + 1;
            }
        }
        else if (indexA < m_inputQueue.size())
        {
            m_input = m_inputQueue[indexA];
            m_camera.pos = m_input.position;
            m_camera.dir = m_input.direction;
        }
        else
        {
            saveFpsHistory();

            m_playingInput = false;
            m_recordingInput = false;
            m_inputStartTime = 0.0f;
        }
    }

    const Vec3 upAxis = Vec3(0.0f, 0.0f, 1.0f);
    const Vec3 cameraRight = -glm::normalize(glm::cross(upAxis, m_camera.dir));
    const Vec3 cameraUp = -glm::cross(m_camera.dir, cameraRight);

    if (!m_playingInput)
    {
        float forward = 0, right = 0, upward = 0;

        if (m_camera.active)
        {
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_W)) forward = 1.0f;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_A)) right = -1.0f;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_S)) forward = -1.0f;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_D)) right = 1.0f;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_SPACE)) upward = 1.0f;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_LEFT_SHIFT)) upward = -1.0f;

            float speed;
            if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_LEFT_CONTROL))
                speed = 200.0f;
            else
                speed = 80.0f;
            
            m_camera.pos += speed * (forward * m_camera.dir + right * cameraRight + upward * upAxis) * dt;
        }
    }
    ImGfx::SetCamera(m_camera.pos, m_camera.dir, cameraRight, cameraUp);

    if (!m_playingInput) // User button input
    {
        m_mouseTimer = glm::max(m_mouseTimer - dt, 0.0f);
        if (m_camera.active && m_mouseButtonPressed && m_mouseTimer == 0.0f)
        {
            m_input.mouseButtonL = glfwGetMouseButton(m_app->GetWindow(), GLFW_MOUSE_BUTTON_1) == GLFW_PRESS;
            m_input.mouseButtonR = glfwGetMouseButton(m_app->GetWindow(), GLFW_MOUSE_BUTTON_2) == GLFW_PRESS;

            if (m_input.mouseButtonL || m_input.mouseButtonR)
                m_mouseTimer = 0.05f;
        }
    }

    // Apply button input:
    handleMouseClick(m_input.mouseButtonL, m_input.mouseButtonR);

    if (m_recordingInput)
    {
        m_input.position = m_camera.pos;
        m_input.direction = m_camera.dir;

        f64 elapsed = currentTime - m_inputStartTime;
        size_t index = size_t(std::round(elapsed * INPUT_RECORDING_RATE)); 

        if (index >= m_inputQueue.size())
        {
            m_inputQueue.resize(index + 1);
            m_inputQueue[index] = m_input;
        }
        else
        {
            m_inputQueue[index].position = m_input.position;
            m_inputQueue[index].direction = m_input.direction;
            m_inputQueue[index].mouseButtonL = m_inputQueue[index].mouseButtonL || m_input.mouseButtonL;
            m_inputQueue[index].mouseButtonR = m_inputQueue[index].mouseButtonR || m_input.mouseButtonR;
        }
    }

    m_input = {};   // Reset input
}


void MainScene::OnRender(Image& image, CommandBuffer& cmdBuffer, f32 alpha)
{
    renderGui();

    m_renderer.SetCamera(m_camera.pos, m_camera.dir, m_camera.fov);

    if (m_svtModified)
    {
        m_renderer.UpdateSparseVoxelTree(m_svt);
        m_svtModified = false;
    }

    m_renderer.Render(image, cmdBuffer);
    m_fpsCounter += 1;
    
/*
    auto swapChain = m_app->GetSwapChain();

    RenderPass renderPass = {
        .Area = swapChain.GetArea(),
        .Attachments = {{
            .Image = &image,
            .ClearValue = { VkClearColorValue{ 0.5f, 0.0f, 0.5f, 1.0f } },
        }}
    };

    cmdBuffer.BeginRenderPass(renderPass);

    ImGuiLayer::Draw(cmdBuffer);

    cmdBuffer.EndRenderPass();*/

}


void MainScene::OnResizeEvent(int width, int height)
{
    m_renderer.Resize(width, height);

    ImGfx::SetViewport(float(width), float(height));
}


bool MainScene::OnKeyEvent(int key, int scancode, int action)
{
    auto window = m_app->GetWindow();

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        m_prevCursorPos = getCursorPosition();
        m_camera.active = !m_camera.active;
        m_app->SetCursorEnabled(!m_camera.active);
    }
    else if (key == GLFW_KEY_TAB && action == GLFW_PRESS)
    {
        m_showInfo = !m_showInfo;
    }
    else if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        m_renderer.ClearVoxelCache();
    }
    else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F8 && action == GLFW_PRESS)
    {
        m_renderer.SetDebug(key - GLFW_KEY_F1);
    }
    else if (key == GLFW_KEY_HOME && action == GLFW_PRESS)
    {
        m_recordingInput = true;
        m_playingInput = false;
        m_inputStartTime = glfwGetTime();
        m_inputQueue.clear();
    }
    else if (key == GLFW_KEY_END && action == GLFW_PRESS)
    {
        saveRecordedInput();

        m_recordingInput = false;
        m_playingInput = false;
        m_inputStartTime = glfwGetTime();
    }
    else if (key == GLFW_KEY_M && action == GLFW_PRESS)
    {
        m_camera.yaw = glm::radians(-90.0f);
        m_camera.pitch = 0.0f;
        m_camera.dir = {
            cos(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.pitch),
        };
        m_camera.pos.x = 32.0f;
    }

    return false;
}


void MainScene::OnMouseMovedEvent(Vec2 cursorPos)
{
    if (m_camera.active)
    {
        Vec2 movement = cursorPos - m_prevCursorPos;
        m_prevCursorPos = cursorPos;

        movement *= 0.002f;

        m_camera.yaw -= movement.x;
        m_camera.pitch -= movement.y;

        m_camera.pitch = glm::clamp(m_camera.pitch, -1.5f, 1.5f);

        Vec3 direction {
            cos(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.yaw) * cos(m_camera.pitch),
            sin(m_camera.pitch),
        };
        m_camera.dir = direction;
    }   
}

void MainScene::OnMouseScrolledEvent(Vec2 offset)
{
    if (offset.y == -1.0f && m_selectedMaterial > 1)
    {
        m_selectedMaterial -= 1;
    }
    else if (offset.y == 1.0f && m_selectedMaterial < 255)
    {
        m_selectedMaterial += 1;
    }
}


bool MainScene::OnMouseButtonEvent(int button, int action)
{
    if (!m_camera.active)
        return false;

    if (action == GLFW_PRESS)
    {
        if (button == GLFW_MOUSE_BUTTON_3)
        {
            auto result = RayCast(m_svt, m_camera.pos, m_camera.dir);
            if (!result.Hit)
                return false;

            LOG_TRACE("Voxel = {}", result.VoxelPtr);
            LOG_TRACE("Voxel pos = {}", result.VoxelPos);
            LOG_TRACE("Position = {}", result.Pos);
            LOG_TRACE("Normal = {}", result.Normal);
            LOG_TRACE("Material = {}", m_selectedMaterial);

            m_selectedMaterial = m_svt.GetVoxels()[result.VoxelPtr].idx;
        }
        else
        {
            if (m_selectedObject && button == GLFW_MOUSE_BUTTON_2)
            {
                auto result = RayCast(m_svt, m_camera.pos, m_camera.dir);
                if (result.Hit)
                {
                    Vec3 pos = Vec3(result.VoxelPos) + result.Normal;
                    pos = glm::clamp(pos, Vec3(0.0f), Vec3(m_svt.GetDimensions()) - 1.0f);

                    m_svt.Modify(pos, m_selectedObject->Voxels);
                    m_svtModified = true;
                }
            }

            //handleMouseClick(button == GLFW_MOUSE_BUTTON_1, button == GLFW_MOUSE_BUTTON_2);
            m_input.mouseButtonL = (button == GLFW_MOUSE_BUTTON_1);
            m_input.mouseButtonR = (button == GLFW_MOUSE_BUTTON_2);

            m_mouseTimer = 0.5f;    // initial delay before `handleMouseClick` can be called again.
            m_mouseButtonPressed = true;
        }
    }
    else if (action == GLFW_RELEASE)
    {
        m_mouseButtonPressed = false;
    }

    return false;
}


void MainScene::handleMouseClick(bool buttonL, bool buttonR)
{
    if (!buttonL && !buttonR)
        return;

    if (m_selectedObject)
        return;

    auto result = RayCast(m_svt, m_camera.pos, m_camera.dir);
    if (!result.Hit)
        return;

    if (buttonL)
    {
        Vec3u pos = glm::clamp(result.VoxelPos, Vec3u(0), Vec3u(pow4(m_svt.GetDepth()) - 1u));
        VoxelArray mask = MakeSphere(m_brushSize, Voxel{ 255 });
        Vec3u min = pos - mask.Size / 2u;
        m_svt.Subtract(min, mask);
        m_svtModified = true;
    }
    else if (buttonR)
    {
        Vec3 pos = Vec3(result.VoxelPos) + result.Normal;
        pos = glm::clamp(pos, Vec3(0), Vec3(pow4(m_svt.GetDepth()) - 1u));
        //pos = glm::clamp(pos, Vec3(0), Vec3(m_svt.GetDimensions() - 1u));

        VoxelArray voxels = MakeSphere(m_brushSize, Voxel{ u8(m_selectedMaterial) });
        Vec3u min = Vec3u(pos) - voxels.Size / 2u;
        m_svt.Modify(min, voxels);
        m_svtModified = true;       
    }
}


void MainScene::renderGui()
{
    ImGuiLayer::BeginFrame();

    // Show fps & camera position
    if (m_showInfo)
    {
        auto imViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(imViewport->WorkPos, ImGuiCond_Always);

        auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::Begin("overlay", 0, flags);

        if (m_recordingInput)
            ImGui::TextColored(ImVec4(1,0,0,1), "[REC]");
        else if (m_playingInput)
            ImGui::TextColored(ImVec4(0,1,0,1), "[REPLAY]");

        ImGui::Text("FPS: %u", m_fps);
        ImGui::Separator();

        ImGui::Text("X: %.3f", m_camera.pos.x);
        ImGui::Text("Y: %.3f", m_camera.pos.y);
        ImGui::Text("Z: %.3f", m_camera.pos.z);

        ImGui::End();
    }

    if (!m_camera.active)
    {
        bool changed = false;
        bool refresh = false;

        ImGui::Begin("Render Parameters");
        changed |= ImGui::ColorEdit3("Sky color", &m_renderParams.SkyColor[0], ImGuiColorEditFlags_DisplayHSV);
        ImGui::SliderAngle("Camera fov", &m_camera.fov, 60.0f, 120.0f);

        ImGui::SeparatorText("Ambient");
        refresh |= ImGui::ColorEdit3("Ambient light", &m_renderParams.AmbientLight[0], ImGuiColorEditFlags_DisplayHSV);
        refresh |= ImGui::SliderFloat("Ambient falloff", &m_renderParams.AmbientFalloff, 1.0f, 512.0f, "%.0f");        
        refresh |= ImGui::SliderFloat("Ambient min", &m_renderParams.AmbientMinimum, 0.01f, 1.0f, "%.2f");

        ImGui::SeparatorText("Sun");
        changed |= ImGui::ColorEdit3("Sun light", &m_renderParams.SunLight[0], ImGuiColorEditFlags_DisplayHSV);
        changed |= ImGui::SliderAngle("Sun elevation", &m_renderParams.SunElevationAngle, 0.0f, 90.0f);
        changed |= ImGui::SliderAngle("Sun azimuth", &m_renderParams.SunAzimuthAngle, 90.0f, 270.0f);
        changed |= ImGui::SliderAngle("Sun cone", &m_renderParams.SunConeAngle, 0.0f, 12.0f);

        ImGui::End();

        if (changed || refresh)
            m_renderer.SetParams(m_renderParams);

        if (refresh)
            m_renderer.ClearVoxelCache();

        materialEditor();
        objectPicker();
    }



    ImGfx::SetDrawList(ImGui::GetBackgroundDrawList());

    if (m_selectedObject)
    {
        auto result = RayCast(m_svt, m_camera.pos, m_camera.dir);
        if (result.Hit)
        {
            Vec3 min = Vec3(result.VoxelPos) + result.Normal;
            Vec3 max = min + Vec3(m_selectedObject->Voxels.Size);

            ImGfx::DrawWireBox(min, max, IM_COL32(255,0,0,255));
        }
    }

    ImGuiLayer::EndFrame();
}


void MainScene::materialEditor()
{
    bool paletteChanged = false;

    ImGui::Begin("Palette");
    const int kPaletteColumns = 16;

    if (ImGui::BeginTable("palette", kPaletteColumns))
    {
        for (int i = 0; i < 256; i++)
        {
            auto *material = &m_palette[i];
            Vec4 color = Vec4(material->color) / 255.0f;

            if (i % kPaletteColumns == 0)
                ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(i % kPaletteColumns);
            
            bool selected = m_selectedMaterial == i && i > 0;

            if (selected)
            {
                float time = glfwGetTime();
                ImVec4 borderColor(1.0f, 1.0f, 1.0f, 1.0f);
                
                float h, s, v;
                ImGui::ColorConvertRGBtoHSV(1.0f - color.r, 1.0f - color.g, 1.0f - color.b, h, s, v);
                ImGui::ColorConvertHSVtoRGB(h, fmodf(time, 1.0f), 1.0f, borderColor.x, borderColor.y, borderColor.z);

                //ImGui::ColorConvertHSVtoRGB(fmodf(time * 0.125f, 1.0f / 6.0f), 1.0f, 1.0f, borderColor.x, borderColor.y, borderColor.z);

                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
            }

            std::string id = "mat_" + std::to_string(i);
            if (ImGui::ColorButton(id.c_str(), ImVec4(color.r, color.g, color.b, 1.0)))
            {
                if (selected)
                    ImGui::OpenPopup("material_editor");
                m_selectedMaterial = i;
            }

            if (selected)
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
        }

        if (ImGui::BeginPopup("material_editor"))
        {
            auto *material = &m_palette[m_selectedMaterial];
            Vec4 color = Vec4(material->color) / 255.0f;

            ImGui::SeparatorText("Base color");
            if (ImGui::ColorPicker3("", &color[0]))
            {
                material->color = Vec4u8(glm::clamp(color * 255.0f, 0.0f, 255.0f));
                paletteChanged = true;
            }

            ImGui::SeparatorText("Properties");
            paletteChanged |= ImGui::SliderFloat("Emission", &material->emission, 0.0f, 50.0f, "%.1f");
            paletteChanged |= ImGui::SliderFloat("Specular", &material->specular, 0.0f, 1.0f, "%.3f");

            ImGui::EndPopup();
        }
        ImGui::EndTable();
    }
    ImGui::LabelText("", "Selected material: %u", m_selectedMaterial);
    ImGui::End();


    if (paletteChanged)
    {
        m_renderer.UpdatePalette(m_palette);
    }
}


void MainScene::objectPicker()
{
    ImGui::Begin("Edit");

    ImGui::SeparatorText("Brush");
    ImGui::BeginDisabled(m_selectedObject != nullptr);
    ImGui::SliderInt("Brush size", &m_brushSize, 1, 10);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Objects");
    if (ImGui::Button("Deselect"))
    {
        m_selectedObject = nullptr;
    }
    if (ImGui::BeginTable("object_table", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("btn", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 60.0f);
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("data", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        
        for (size_t i = 0; i < m_objects.size(); i++)
        {
            auto& object = m_objects[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (object->IsLoaded())
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7,0.1,0.1,1.0));
                ImGui::Button("Unload", ImVec2(60.0f, 0.0f));
            }
            else
            {
                bool disabled = m_objectIsLoading.load();

                if (disabled)
                    ImGui::BeginDisabled();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2,0.7,0.2,1.0));
                if (ImGui::Button("Load", ImVec2(60.0f, 0.0f)))
                {
                    m_objectLoader = std::async(std::launch::async, [this, &object]()
                    {
                        m_objectIsLoading.store(true);

                        LOG_INFO("Loading \"{}\" ...", object->Name);
                        loadModel(std::format("data/{}.vox", object->Name), &object->Voxels);
                        //std::this_thread::sleep_for(std::chrono::seconds(3));
                        
                        object->Loaded.store(true);
                        m_objectIsLoading.store(false);
                    });
                }

                if (disabled)
                    ImGui::EndDisabled();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();

            ImGui::TableNextColumn();
            if (object->IsLoaded())
            {
                if (ImGui::Selectable(object->Name.c_str(), m_selectedObject == object.get(), ImGuiSelectableFlags_SpanAllColumns))
                    m_selectedObject = object.get();
            }
            else
            {
                ImGui::Text("%s", object->Name.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s", FmtBytes(object->Voxels.Data.size()).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}


void MainScene::loadMap(std::string path)
{
    const std::string svtPath = path + ".svt";
    const std::string voxPath = path + ".vox";

    bool loaded = false;
    std::ifstream svtFile(svtPath, std::ios::in | std::ios::binary);

    if (svtFile.good())
    {
        loaded = m_svt.Load(svtFile);
        if (!loaded)
            LOG_ERROR("Failed to load '{}'", svtPath);

        svtFile.close();
    }

    if (true)
    {
        auto voxData = FileSystem::ReadFile(voxPath);

        if (voxData)
        {
            auto *scene = ogt_vox_read_scene(voxData->data(), voxData->size());

            for (int i = 0; i < 256; i++)
            {
                memcpy(&m_palette[i].color, scene->palette.color + i, 4);
                m_palette[i].emission = scene->materials.matl[i].emit * 10.0f;

                if (std::isnan(m_palette[i].emission) || std::isinf(m_palette[i].emission))
                {
                    m_palette[i].emission = 0.0f;
                }
#if GAMMA_CORRECT
                ColorCorrect(m_palette[i].color);
#endif
                
                if (scene->materials.matl[i].rough < 0.05)
                {
                    if (scene->materials.matl[i].spec > 0.0)
                    {
                        m_palette[i].specular = scene->materials.matl[i].spec;
                    }
                    else if (scene->materials.matl[i].metal > 0.0)
                    {
                        m_palette[i].specular = scene->materials.matl[i].metal;
                    }
                    else if (scene->materials.matl[i].ior > 0.0)
                    {
                        m_palette[i].specular = scene->materials.matl[i].ior;
                    }
                    m_palette[i].specular *= (1.0 - scene->materials.matl[i].rough);
                }
            }

            if (!loaded)
            {
                LOG_TRACE("Loading map \"{}\" ...", voxPath);
                VoxelArray voxels;
                VoxLoader::Load(scene, &voxels);
                //m_svt = SVTree(voxels);
                
                LOG_TRACE("Map size = {}", FmtBytes(voxels.Data.size()));
                SVBuilder builder;
                m_svt = builder.Build(voxels);
                
                if (!FileSystem::Exists(svtPath))
                {
                    LOG_TRACE("Saving map \"{}\" ...", svtPath);

                    std::ofstream outputFile(svtPath, std::ios::out | std::ios::binary);
                    m_svt.Dump(outputFile);
                }
            }
        }
    }

    size_t nodeCount = m_svt.GetNodes().size();
    size_t voxelCount = m_svt.GetVoxels().size();

    LOG_TRACE("SVT nodes  = {} ({})", nodeCount, FmtBytes(nodeCount * sizeof(SVNode)));
    LOG_TRACE("SVT voxels = {} ({})", voxelCount, FmtBytes(voxelCount * sizeof(Voxel)));
    LOG_TRACE("SVT depth  = {}", m_svt.GetDepth());
    LOG_TRACE("SVT size   = {} x {} x {}", m_svt.GetDimensions().x, m_svt.GetDimensions().y, m_svt.GetDimensions().z);
}


void MainScene::loadModel(std::string path, VoxelArray* model)
{
    auto voxData = FileSystem::ReadFile(path);
    if (!voxData)
        return;

    auto *scene = ogt_vox_read_scene(voxData->data(), voxData->size());
    if (!scene)
        return;

    VoxLoader::Load(scene, model);

    std::bitset<256> paletteMask;
    for (size_t i = 0; i < model->Data.size(); i++)
    {
        paletteMask.set(model->Data[i].idx, true);
    }

    // For each color used by the model, 
    // match it to the closest color from the scene palette:
    for (size_t index = 0; index < 256; index++)
    {
        if (index == 0 || !paletteMask[index])
            continue;
        
        ogt_vox_rgba rgba = scene->palette.color[index];
        Vec3 colorA = Vec3(rgba.r, rgba.g, rgba.b) / 255.0f;

        std::array<float, 256> colorDist;
        for (size_t i = 0; i < 256; i++)
        {
            Vec3 colorB = Vec3(m_palette[i].color) / 255.0f;
            colorDist[i] = ColorDistance(colorA, colorB);
        }

        size_t minIndex = std::min_element(colorDist.begin(), colorDist.end()) - colorDist.begin();

        LOG_TRACE("{} --> {}", index, minIndex);

        for (size_t i = 0; i < model->Data.size(); i++)
        {
            if (model->Data[i].idx == index)
                model->Data[i].idx = minIndex;
        }
    }
}


void MainScene::addObject(std::string name)
{
    m_objects.push_back(std::make_unique<VoxelObject>(name));
}


void MainScene::saveRecordedInput()
{
    if (m_inputQueue.empty())
        return;

    u64 time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    std::string path = std::format("data/recordings/{}-{}.irec", m_mapName, time);

    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file.good())
        LOG_ERROR("Failed to save recording!");
    
    file << "IREC";
    IO::WriteU64(file, m_inputQueue.size());
    file.write((char*)m_inputQueue.data(), m_inputQueue.size() * sizeof(Input));
    file.close();

    LOG_INFO("Saved recording: '{}'", path);
}


bool MainScene::loadRecordedInput(std::string path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.good()) return false;

    char signature[5] = {};
    if (!IO::ReadChar(file, signature + 0)) return false;
    if (!IO::ReadChar(file, signature + 1)) return false;
    if (!IO::ReadChar(file, signature + 2)) return false;
    if (!IO::ReadChar(file, signature + 3)) return false;
    if (strcmp(signature, "IREC") != 0)
        return false;

    u64 inputCount;
    if (!IO::ReadU64(file, &inputCount)) return false;

    m_inputQueue.resize(inputCount);
    if (!IO::ReadArray(file, &m_inputQueue[0], inputCount, sizeof(Input)))
        return false;

    file.close();
    return true;
}


void MainScene::saveFpsHistory()
{
    if (m_fpsHistory.empty())
        return;

    u64 time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    std::string path = std::format("{}-{}.json", m_mapName, time);

    std::ofstream file(path, std::ios::out);
    if (!file.good()) return;

    file << "{\n";
    file << "\t\"device\": \"" << m_device->GetDebugDeviceName() << "\",\n";
    file << "\t\"map\": \"" << m_mapName << "\",\n";
    file << "\t\"rec\": \"" << m_inputRecName << "\",\n";
    file << "\t\"seconds\": " << (glfwGetTime() - m_inputStartTime) << ",\n";
    
    file << "\t\"fps\": [ ";
    file << m_fpsHistory[0];
    for (auto it = m_fpsHistory.begin() + 1; it < m_fpsHistory.end(); ++it)
    {
        file << ", " << *it;;
    }
    file << "]\n}";

    file.close();
}