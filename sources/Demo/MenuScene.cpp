#include "MenuScene.h"
#include "MainScene.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include "Base/FileSystem.h"
#include "Renderer/CommandBuffer.h"

#include <imgui.h>
#include <GLFW/glfw3.h>



MenuScene::MenuScene()
{
    addMap("nuke");
    addMap("oasis");
    addMap("room");
    addMap("sponza");
    addMap("town");
    addMap("city");
    addMap("terrain");
    addMap("terrain-2x");
    addMap("terrain-4x");
    addMap("adamantis");
    addMap("apterra");
    addMap("xmas");
    addMap("wolfpine");
    addMap("tree");
    addMap("winter_tree");
    addMap("cornell_box");
    addMap("lights_test");
    addMap("hairball-1k");
    addMap("hairball-2k");
    addMap("hairball-3k");
    addMap("bistro-512");
    addMap("bistro-2k");
    addMap("bistro-4k");
    addMap("Trident_Cliffs_City");
    addMap("san-miguel");
    addMap("cathedral");

    m_recordings.push_back(u8"---");
    for (const auto& entry : std::filesystem::directory_iterator("data/recordings/"))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".irec")
            m_recordings.push_back(entry.path().filename().u8string());
    }
}


void MenuScene::OnAttach()
{
    m_app->SetCursorEnabled(true);
}


void MenuScene::OnDetach()
{

}


void MenuScene::OnUpdate(f32 dt)
{
    if (m_mainScene && m_mainScene->IsReady())
    {
        m_app->Transition(m_mainScene);
    }
}


void MenuScene::OnRender(Image& image, CommandBuffer& cmdBuffer, f32 alpha)
{
    ImGuiLayer::BeginFrame();

    ImVec2 center = ImVec2(m_swapChain->GetSize().width * 0.5f, m_swapChain->GetSize().height * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 260), ImGuiCond_Always);
    ImGui::Begin("1", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::BeginDisabled(!!m_mainScene);
    ImGui::Combo("Select map", &m_mapIndex, m_maps.data(), m_maps.size());

    ImGui::Combo("Select recording", &m_recordingIndex, [](void* self, int idx)
    {
        return (const char*)((MenuScene*)self)->m_recordings[idx].c_str();
    }, this, m_recordings.size());

    ImGui::Checkbox("Specular lighting", &m_enableSpecular);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!!m_mainScene || m_mapIndex < 0 || m_mapIndex >= m_maps.size());
    ImGui::SetCursorPosY(260 - ImGui::GetStyle().FramePadding.y * 2 - ImGui::CalcTextSize("A").y - ImGui::GetStyle().WindowPadding.y);
    if (ImGui::Button("Play"))
    {
        play();
    }
    ImGui::EndDisabled();

    ImGui::End();

    ImGuiLayer::EndFrame();

    //----------------------------------------------------------------------------------

    auto swapChain = m_app->GetSwapChain();

    RenderPass renderPass = {
        .Area = swapChain.GetArea(),
        .Attachments = {{
            .Image = &image,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.5f, 1.0f } },
        }}
    };

    cmdBuffer.BeginRenderPass(renderPass);

    ImGuiLayer::Draw(cmdBuffer);

    cmdBuffer.EndRenderPass();
}


bool MenuScene::OnKeyEvent(int key, int scancode, int action)
{
    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS)
    {
        if (!!m_mainScene || m_mapIndex < 0 || m_mapIndex >= m_maps.size())
            return false;
        
        play();
    }
    return false;
}


void MenuScene::play()
{
    m_mainScene = std::make_shared<MainScene>(
        std::string(m_maps[m_mapIndex]),
        m_recordingIndex == 0 ? "" : (char*)m_recordings[m_recordingIndex].c_str(),
        m_enableSpecular
    );
}


void MenuScene::addMap(const char *map)
{
    if (FileSystem::Exists("data/" + std::string(map) + ".vox"))
    {
        m_maps.push_back(map);
    }
}