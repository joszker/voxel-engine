#pragma once
#include "Application.h"


/*
    Layer responsible for managing Dear ImGui.
*/
class ImGuiLayer
{
public:
    static void Init(Application& app);
    static void Shutdown();

    static void BeginFrame();
    static void EndFrame();
    static void Draw(CommandBuffer& cmdBuffer);
};