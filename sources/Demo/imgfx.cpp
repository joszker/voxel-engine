#include "imgfx.h"


namespace ImGfx
{

static float s_width;
static float s_height;
static float s_aspect;

static Vec3 s_cameraPos; 
static Vec3 s_cameraDir;
static Vec3 s_cameraRight;
static Vec3 s_cameraUp;

static ImDrawList *s_drawList;

void SetViewport(float width, float height)
{
    s_width = width;
    s_height = height;
    s_aspect = width / height;
}

void SetCamera(Vec3 cameraPos, Vec3 cameraDir, Vec3 cameraRight, Vec3 cameraUp)
{
    s_cameraPos = cameraPos;
    s_cameraDir = cameraDir;
    s_cameraRight = cameraRight;
    s_cameraUp = cameraUp;
}

void SetDrawList(ImDrawList* drawList)
{
    s_drawList = drawList;
}


static bool ProjectPoint(Vec3 world, ImVec2& out)
{
    glm::vec3 d = world - s_cameraPos;

    float z = glm::dot(d, s_cameraDir);

    // Behind camera.
    if (z <= 0.0f)
        return false;

    float invZ = 1.0f / z;
    float sx = glm::dot(d, s_cameraRight) * invZ;
    float sy = glm::dot(d, s_cameraUp) * invZ * s_aspect;

    out.x = (sx * 0.5f + 0.5f) * s_width;
    out.y = (1.0f - (sy * 0.5f + 0.5f)) * s_height;

    return true;
}

static bool ProjectLine(Vec3 a, Vec3 b, ImVec2& outA, ImVec2& outB)
{
    glm::vec3 da = a - s_cameraPos;
    glm::vec3 db = b - s_cameraPos;

    float za = glm::dot(da, s_cameraDir);
    float zb = glm::dot(db, s_cameraDir);

    constexpr float nearPlane = 1e-5f; // Avoids projecting exactly at z = 0.

    // Entirely behind camera.
    if (za <= nearPlane && zb <= nearPlane)
        return false;

    // Clip segment against z = epsilon.
    if (za <= nearPlane || zb <= nearPlane)
    {
        float t = (nearPlane - za) / (zb - za);

        glm::vec3 p = da + t * (db - da);

        if (za <= nearPlane)
        {
            da = p;
            za = nearPlane;
        }
        else
        {
            db = p;
            zb = nearPlane;
        }
    }

    // Project A.
    float xa = glm::dot(da, s_cameraRight) / za;
    float ya = (glm::dot(da, s_cameraUp) / za) * s_aspect;

    // Project B.
    float xb = glm::dot(db, s_cameraRight) / zb;
    float yb = (glm::dot(db, s_cameraUp) / zb) * s_aspect;

    // NDC [-1, 1] -> screen.
    outA.x = (xa * 0.5f + 0.5f) * s_width;
    outA.y = (1.0f - (ya * 0.5f + 0.5f)) * s_height;

    outB.x = (xb * 0.5f + 0.5f) * s_width;
    outB.y = (1.0f - (yb * 0.5f + 0.5f)) * s_height;

    return true;
}

static bool ClipLineNearPlane(Vec3& a, Vec3& b)
{
    float nearPlane = 0.001f;

    bool aInside = a.z > nearPlane;
    bool bInside = b.z > nearPlane;

    if (!aInside && !bInside)
        return false;

    if (aInside && bInside)
        return true;

    float t = (nearPlane - a.z) / (b.z - a.z);

    glm::vec3 p = a + t * (b - a);

    if (!aInside)
        a = p;
    else
        b = p;

    return true;
}

void DrawLine(Vec3 a, Vec3 b, ImU32 color, float thickness)
{
    ImVec2 sca, scb;
    if (!ProjectLine(a, b, sca, scb))
        return;

    s_drawList->AddLine(sca, scb, color, thickness);
}

void DrawWireBox(Vec3 min, Vec3 max, ImU32 color, float thickness)
{
    Vec3 c[8] =
    {
        { min.x, min.y, min.z },
        { max.x, min.y, min.z },
        { max.x, max.y, min.z },
        { min.x, max.y, min.z },

        { min.x, min.y, max.z },
        { max.x, min.y, max.z },
        { max.x, max.y, max.z },
        { min.x, max.y, max.z }
    };

    DrawLine(c[0], c[1], color, thickness);
    DrawLine(c[1], c[2], color, thickness);
    DrawLine(c[2], c[3], color, thickness);
    DrawLine(c[3], c[0], color, thickness);

    DrawLine(c[4], c[5], color, thickness);
    DrawLine(c[5], c[6], color, thickness);
    DrawLine(c[6], c[7], color, thickness);
    DrawLine(c[7], c[4], color, thickness);

    DrawLine(c[0], c[4], color, thickness);
    DrawLine(c[1], c[5], color, thickness);
    DrawLine(c[2], c[6], color, thickness);
    DrawLine(c[3], c[7], color, thickness);
}

}