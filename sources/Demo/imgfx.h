#pragma once
#include "Base/Base.h"
#include <imgui.h>

// immediate mode 3D graphics
namespace ImGfx 
{

void SetViewport(float width, float height);
void SetCamera(Vec3 cameraPos, Vec3 cameraDir, Vec3 cameraRight, Vec3 cameraUp);
void SetDrawList(ImDrawList* drawList);

void DrawLine(Vec3 a, Vec3 b, ImU32 color, float thickness = 1.0f);

void DrawWireBox(Vec3 min, Vec3 max, ImU32 color, float thickness = 1.0f);

}
