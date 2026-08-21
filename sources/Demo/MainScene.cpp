#include "MainScene.h"
#include "Application.h"
#include "Base/Log.h"
#include "Base/FileSystem.h"
#include "Renderer/DescriptorSet.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/bit.hpp>

#include "Voxel/SVTree.h"
#include "Voxel/SVBuilder.h"
#include "Voxel/VoxLoader.h"
#include "Voxel/RayTrace.h"

#include "Demo/imgfx.h"

#include "3rdparty/ogt_vox.h"

#define STB_IMAGE_IMPLEMENTATION
#include "3rdparty/stb_image.h"

#include <bitset>

struct GPU_SVTree
{
    //u32 root;
    u32 voxelCount;
    u32 depth;
    VkDeviceAddress nodes;      // SVNode*
    VkDeviceAddress voxels;     // Voxel* 
};

#define VHM_SIZE            16777216
#define VHM_ENTRY_SIZE      32
constexpr int VHM_BUFFER_SIZE = (VHM_SIZE * (4 + VHM_ENTRY_SIZE));



struct GPU_VoxelHashMap
{
    VkDeviceAddress keys;
    VkDeviceAddress values;
};

struct PushConstants
{
    Vec4 AmbientLight;
    Vec3 SunDir;
    f32 SunConeThetaCos;
    Vec4 SunLight;

    Vec4 CameraPos;
    Vec4 CameraDir;
    Vec2 Resolution;
    u32 FrameNo;
    u32 CurrentGen;

    GPU_SVTree VoxelMap;
    GPU_VoxelHashMap CurrentVHM;
    GPU_VoxelHashMap PrevVHM;
};


constexpr int X = offsetof(PushConstants, FrameNo);




VoxelArray MakeSphere(int r, Voxel color);

void MainScene::OnAttach()
{
    if (true)
    {
        loadMap("data/nuke");
    }
    else
    {
        m_svt = SVTree(MakeSphere(4, Voxel{ 255 }));
        for (uint i = 0; i < 256; i++)
        {
            m_palette[i].color[0] = 128;
        }
    }

    m_camera.pos = Vec3(0.0f, 0.0f, 0.0f);
    m_camera.yaw = glm::half_pi<f32>();
    m_camera.dir = {
        cos(m_camera.yaw) * cos(m_camera.pitch),
        sin(m_camera.yaw) * cos(m_camera.pitch),
        sin(m_camera.pitch),
    };
    m_camera.active = true;

    {
        double x, y;
        glfwGetCursorPos(m_app->GetWindow(), &x, &y);
        m_prevCursorPos = Vec2(x, y);
    }

    initImGui();

    /* Create sampler */
    {
        VkSamplerCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 0.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = {},
            .unnormalizedCoordinates = VK_FALSE,
        };

        vkCreateSampler(*m_device, &info, nullptr, &m_sampler);
    }

    /* Create descriptor layout */
    DescriptorLayout layout;
    {
        DescriptorLayoutBuilder layoutBuilder;
        //layoutBuilder.Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        layoutBuilder.Add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        layoutBuilder.Add(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        layout = layoutBuilder.Build(*m_device, VK_SHADER_STAGE_ALL);
    }


    {
        DescriptorAllocator descriptorAlloc;

        m_frames.resize(m_swapChain->GetFrameCount());
        for (Frame& frame : m_frames)
        {
            descriptorAlloc.Add(&layout, &frame.descriptorSet);
        }

        m_descriptorPool = descriptorAlloc.Allocate(*m_device);
    }
    createImages(m_swapChain->GetSize().width, m_swapChain->GetSize().height);

    /* Load shaders */
    VkShaderModule fullscreenShader = loadShader("data/shaders/fullscreen.vert.spv");
    VkShaderModule primaryShader = loadShader("data/shaders/primary.frag.spv");
    VkShaderModule compositeShader = loadShader("data/shaders/composite.frag.spv");
    VkShaderModule averagingShader = loadShader("data/shaders/average.comp.spv");
    VkShaderModule purgeShader = loadShader("data/shaders/purge.comp.spv");
    //VkShaderModule blurShader = loadShader("data/shaders/blur.frag.spv");

    /* Create primary pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(primaryShader);
        //pipelineBuilder.AddColorAttachment({ m_frames[0].primaryImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].voxelImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].normalImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].radianceImage.GetFormat() });
        pipelineBuilder.AddColorAttachment({ m_frames[0].specularSunImage.GetFormat() });

        pipelineBuilder.AddDescriptorLayout(layout);
        pipelineBuilder.AddPushConstantRange({
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(PushConstants),
        });

        m_primaryPass = pipelineBuilder.Build(*m_device);
    }

#if 0
    /* Create blur pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(blurShader);
        pipelineBuilder.AddColorAttachment({ m_frames[0].blurImage.GetFormat() });

        pipelineBuilder.AddDescriptorLayout(layout);
        pipelineBuilder.AddPushConstantRange({
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(PushConstants),
        });

        m_blurPass = pipelineBuilder.Build(*m_device);
    }
#endif


    /* Create composite pipeline */
    {
        PipelineBuilder pipelineBuilder;

        pipelineBuilder.AddVertexShader(fullscreenShader);
        pipelineBuilder.AddFragmentShader(compositeShader);
        pipelineBuilder.AddColorAttachment({ m_swapChain->GetImageFormat() });
        pipelineBuilder.AddDescriptorLayout(layout);
        pipelineBuilder.AddPushConstantRange({
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(PushConstants),
        });

        m_compositePass = pipelineBuilder.Build(*m_device);
    }

    {
        std::array pcRanges = {
            VkPushConstantRange {
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                .size = sizeof(PushConstants),
            },
        };
        
        ComputePipelineDesc desc = {
            .ShaderModule = averagingShader,
            .PushConstantRanges = pcRanges,
        };

        m_averagingPass = CreateComputePipeline(*m_device, desc);
    }

    {
        std::array pcRanges = {
            VkPushConstantRange {
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                .size = sizeof(PushConstants),
            },
        };
        
        ComputePipelineDesc desc = {
            .ShaderModule = purgeShader,
            .PushConstantRanges = pcRanges,
        };

        m_purgePip = CreateComputePipeline(*m_device, desc);
    }

    vkDestroyShaderModule(*m_device, fullscreenShader, nullptr);
    vkDestroyShaderModule(*m_device, primaryShader, nullptr);
    vkDestroyShaderModule(*m_device, compositeShader, nullptr);

    /* Storage buffers */
    {
        const size_t size = 
            sizeof(SVNode) * (m_svt.GetNodes().size() + 8 * 4096) + 
            sizeof(Voxel) * (m_svt.GetVoxels().size() + 8 * 16384);

        m_svtBuffer.gpu = m_device->CreateBuffer({
            .Size = size,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        });

        m_svtBuffer.cpu = m_device->CreateBuffer({
            .Size = size,
            .Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .AllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        });
    }

    {
        /*
        m_voxelFaceBuffer = m_device->CreateBuffer({
            .Size = 1920 * 1080 * 16,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });*/

        m_vhmBuffer = m_device->CreateBuffer({
            .Size = 2 * VHM_BUFFER_SIZE,
            .Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .AllocFlags = 0,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });

        m_paletteBuffer = m_device->CreateBuffer({
            .Size = sizeof(m_palette[0]) * m_palette.size(),
            .Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .AllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .AllocType = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        });
        updatePalette();
    }

    {
        int width, height, channels;
        u8 *stbnTexRGBA = stbi_load("data/stbn_vec2.png", &width, &height, &channels, 4);


        if (stbnTexRGBA)
        {
            std::vector<u8> stbnTexRG;
            stbnTexRG.resize(width * height * 2);

            for (size_t i = 0; i < width * height; i++)
            {
                stbnTexRG[2 * i + 0] = stbnTexRGBA[4 * i + 0];
                stbnTexRG[2 * i + 1] = stbnTexRGBA[4 * i + 1];
            }
            stbi_image_free(stbnTexRGBA);

            m_stbnImage = m_device->CreateImage({
                .Type = VK_IMAGE_TYPE_2D,
                .Format = VK_FORMAT_R8G8_UNORM,
                .Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .Size = { (u32)width, (u32)height, 1 },
                .MipLevels = 1,
                .ViewType = VK_IMAGE_VIEW_TYPE_2D,
            });

            m_device->UploadImage(&m_stbnImage, stbnTexRG.data(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    m_prevTime = glfwGetTime();

    m_currentVHM = 0;
    m_prevVHM = m_currentVHM + VHM_BUFFER_SIZE;

    m_ambientLight = Vec3(0.8);
    m_sunLight = Vec3(0.5);

    m_sunElevation = glm::radians(75.0f);
    m_sunAzimuth = glm::radians(90.0f);
//    m_sunElevation = glm::radians(50.0f);
//    m_sunAzimuth = glm::radians(140.0f);
    m_sunConeAngle = glm::radians(2.0f);

//    m_skyColor = { 0.525f, 0.804f, 0.918f };
    m_skyColor = { 0.3686274509803922, 0.6392156862745098, 0.7764705882352941 };


    m_camera.projection = glm::perspective(glm::radians(90.0f), 1920.0f / 1080.0f, 0.01f, 2000.0f);
    //m_camera.projection[1][1] *= -1.0f;

    m_objects.push_back(std::make_unique<VoxelObject>("dragon"));
    m_objects.push_back(std::make_unique<VoxelObject>("teapot"));
    m_objects.push_back(std::make_unique<VoxelObject>("winter_tree"));
    m_objects.push_back(std::make_unique<VoxelObject>("3story"));
    m_objects.push_back(std::make_unique<VoxelObject>("castle"));
    m_objects.push_back(std::make_unique<VoxelObject>("gladius"));
    m_objects.push_back(std::make_unique<VoxelObject>("smallTree"));
}


void MainScene::OnDetach()
{
    deinitImGui();
    m_objectLoader.wait();
}


void MainScene::OnUpdate(f32 dt)
{
    f64 currentTime = glfwGetTime();
    
    if (floor(currentTime * 4.0) > floor(m_prevTime * 4.0))
    {
        f64 frameTime = currentTime - m_prevTime;
        m_fps = uint(1.0 / frameTime);
    }
    m_prevTime = currentTime;

    const Vec3 upAxis = Vec3(0.0f, 0.0f, 1.0f);

    const Vec3 cameraRight = -glm::normalize(glm::cross(upAxis, m_camera.dir));
    const Vec3 cameraUp = -glm::cross(m_camera.dir, cameraRight);


    if (m_camera.active)
    {
        float forward = 0.0f;
        float right = 0.0f;
        float upward = 0.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_W))
            forward = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_A))
            right = -1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_S))
            forward = -1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_D))
            right = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_SPACE))
            upward = 1.0f;

        if (glfwGetKey(m_app->GetWindow(), GLFW_KEY_LEFT_SHIFT))
            upward = -1.0f;

        m_camera.pos += 80.0f * (forward * m_camera.dir + right * cameraRight + upward * upAxis) * dt;
    }

    
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            auto imViewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(imViewport->WorkPos, ImGuiCond_Always);

            auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            ImGui::Begin("overlay", 0, flags);

            ImGui::Text("FPS: %u", m_fps);
            ImGui::Separator();

            ImGui::Text("X: %.3f", m_camera.pos.x);
            ImGui::Text("Y: %.3f", m_camera.pos.y);
            ImGui::Text("Z: %.3f", m_camera.pos.z);

            ImGui::End();
        }

        if (!m_camera.active)
        {
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            ImGui::Begin("Lighting");

            ImGui::ColorEdit3("Sky color", &m_skyColor[0], ImGuiColorEditFlags_DisplayHSV);
            ImGui::ColorEdit3("Ambient light", &m_ambientLight[0], ImGuiColorEditFlags_DisplayHSV);
            ImGui::ColorEdit3("Sun light", &m_sunLight[0], ImGuiColorEditFlags_DisplayHSV);

            ImGui::SliderAngle("Sun elevation", &m_sunElevation, 0.0f, 90.0f);
            ImGui::SliderAngle("Sun azimuth", &m_sunAzimuth, 90.0f, 270.0f);
            ImGui::SliderAngle("Sun cone", &m_sunConeAngle, 0.0f, 12.0f);

            ImGui::End();

            ImGui::Begin("Palette");
            const int kPaletteColumns = 16;
            m_paletteChanged = false;

            if (ImGui::BeginTable("palette", kPaletteColumns))
            {
                for (int i = 0; i < 256; i++)
                {
                    auto *material = &m_palette[i];
                    Vec4 color = Vec4(material->color) / 255.0f;

                    if (i % kPaletteColumns == 0)
                        ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(i % kPaletteColumns);
                    
                    bool selected = m_selectedMaterial == i;

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
                        m_paletteChanged = true;
                    }

                    ImGui::SeparatorText("Properties");
                    m_paletteChanged |= ImGui::SliderFloat("Emission", &material->emission, 0.0f, 50.0f, "%.0f");

                    ImGui::EndPopup();
                }
                ImGui::EndTable();
            }
            ImGui::LabelText("", "Selected material: %u", m_selectedMaterial);
            ImGui::End();

            
            ImGui::Begin("Objects");
            if (ImGui::Button("Load"))
            {

            }
            ImGui::Separator();
            if (ImGui::BeginTable("split2", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
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

        #if 0
        Mat4 view = glm::lookAt(m_camera.pos, m_camera.pos + m_camera.dir, Vec3(0, 0, 1));
        
        //Vec3 p1 = m_camera.pos + m_camera.dir - cameraRight;
        //Vec3 p2 = m_camera.pos + m_camera.dir + cameraRight;
        //Vec3 p1 = Vec3(300, 142, 252) - Vec3(10, 0, 0);
        //Vec3 p2 = Vec3(300, 142, 252) + Vec3(10, 0, 0);
        Vec3 p1 = Vec3(1, 0, 169);
        Vec3 p2 = Vec3(1, 0, 10000);

        Vec4 c1 = m_camera.projection * view * glm::vec4(p1, 1.0f);
        Vec4 c2 = m_camera.projection * view * glm::vec4(p2, 1.0f);

        glm::vec3 ndc1 = glm::vec3(c1) / c1.w;
        glm::vec3 ndc2 = glm::vec3(c2) / c2.w;

        //LOG_TRACE("CLIP: {} {} {} {}", clip.x, clip.y, clip.z, clip.w);
        LOG_TRACE("NDC: {} {} {}", ndc1.x, ndc1.y, ndc1.z);

        

        ImVec2 dc1 = ImVec2((ndc1.x * 0.5f + 0.5f) * 1280.0f, (1.0f - (ndc1.y * 0.5f + 0.5f)) * 720.0f);
        ImVec2 dc2 = ImVec2((ndc2.x * 0.5f + 0.5f) * 1280.0f, (1.0f - (ndc2.y * 0.5f + 0.5f)) * 720.0f);
        
        drawList->AddCircleFilled(dc1, 5.0f, IM_COL32(0,255,0,255));
        #endif

        /*
        ImVec2 dc1;
        if (ProjectPoint(Vec3(0, 0, 165), m_camera.pos, m_camera.dir, cameraRight, cameraUp, 1280.0f, 720.0f, dc1))
        {

            ImDrawList *drawList = ImGui::GetBackgroundDrawList();
            drawList->AddCircleFilled(dc1, 5.0f, IM_COL32(0,255,0,255));
        }*/

        //drawList->AddLine(dc1, dc2, IM_COL32(0,255,0,255));
        ImGfx::SetViewport(1280.0f, 720.0f);
        ImGfx::SetCamera(m_camera.pos, m_camera.dir, cameraRight, cameraUp);
        ImGfx::SetDrawList(ImGui::GetBackgroundDrawList());

        if (m_editMode && m_selectedObject)
        {
            auto result = RayTrace(m_svt, m_camera.pos, m_camera.dir);
            if (result.Hit)
            {
                Vec3 min = Vec3(result.VoxelPos) + result.Normal;
                Vec3 max = min + Vec3(m_selectedObject->Voxels.Size);

                ImGfx::DrawWireBox(min, max, IM_COL32(255,0,0,255));
            }
        }

        //ImGfx::DrawLine(Vec3(0, 0, 165), Vec3(1, 1, 166), IM_COL32(255,0,0,255));

        ImGui::Render();
    }

    m_mouseTimer = glm::max(m_mouseTimer - dt, 0.0f);
    if (m_camera.active && m_mouseButtonPressed && m_mouseTimer == 0.0f)
    {
        bool mouseButtonL = glfwGetMouseButton(m_app->GetWindow(), GLFW_MOUSE_BUTTON_1) == GLFW_PRESS;
        bool mouseButtonR = glfwGetMouseButton(m_app->GetWindow(), GLFW_MOUSE_BUTTON_2) == GLFW_PRESS;

        if (mouseButtonL || mouseButtonR)
        {
            handleMouseClick(mouseButtonL, mouseButtonR);
            m_mouseTimer = 0.05f;
        }
    }


}

void MainScene::handleMouseClick(bool buttonL, bool buttonR)
{
    auto result = RayTrace(m_svt, m_camera.pos, m_camera.dir);
    if (!result.Hit)
        return;

    if (buttonL)
    {
        Vec3u pos = glm::clamp(result.VoxelPos, Vec3u(0), Vec3u(m_svt.GetDimensions() - 1u));
        VoxelArray mask = MakeSphere(5, Voxel{ 255 });
        Vec3u min = pos - mask.Size / 2u;
        m_svt.Subtract(min, mask);
        m_svtModified = true;
    }
    else if (buttonR)
    {
        Vec3 pos = Vec3(result.VoxelPos) + result.Normal;
        pos = glm::clamp(pos, Vec3(0), Vec3(m_svt.GetDimensions() - 1u));

        VoxelArray voxels = MakeSphere(5, Voxel{ u8(m_selectedMaterial) });
        Vec3u min = Vec3u(pos) - voxels.Size / 2u;
        m_svt.Modify(min, voxels);
        m_svtModified = true;       
    }    
}


void MainScene::OnRender(Image& dstImage, CommandBuffer& cmdBuffer)
{
    Frame& frame = m_frames[m_swapChain->GetCurrentFrameIndex()];

    //std::swap(m_currentVHM, m_prevVHM);

    PushConstants pc;
    {
        pc.AmbientLight = Vec4(m_ambientLight, 0.0f);
        pc.SunLight = Vec4(m_sunLight, 0.0f);

        pc.SunDir.x = cos(m_sunElevation) * sin(m_sunAzimuth);
        pc.SunDir.y = cos(m_sunElevation) * cos(m_sunAzimuth);
        pc.SunDir.z = sin(m_sunElevation);
        pc.SunDir = glm::normalize(pc.SunDir);
        pc.SunConeThetaCos = cos(m_sunConeAngle);
        //Vec3(0.2570805518921551, 0.0, 0.9663899781345132);

        pc.CameraPos = Vec4(m_camera.pos, 0.0f);
        pc.CameraDir = Vec4(m_camera.dir, 0.0f);

        int width, height;
        glfwGetFramebufferSize(m_app->GetWindow(), &width, &height);
        pc.Resolution = Vec2(width, height);
        pc.FrameNo = m_frameNum++;
        pc.CurrentGen = m_gen;

        pc.VoxelMap.nodes = m_svtBuffer.gpu.GetDeviceAddress();
        pc.VoxelMap.voxels = pc.VoxelMap.nodes + sizeof(SVNode) * m_svt.GetNodes().size();
        pc.VoxelMap.depth = m_svt.GetDepth();
        pc.VoxelMap.voxelCount = m_svt.GetVoxels().size();


        pc.CurrentVHM.keys = m_vhmBuffer.GetDeviceAddress() + m_currentVHM;
        pc.CurrentVHM.values = pc.CurrentVHM.keys + sizeof(u32) * VHM_SIZE;

        pc.PrevVHM.keys = m_vhmBuffer.GetDeviceAddress() + m_prevVHM;
        pc.PrevVHM.values = pc.PrevVHM.keys + sizeof(u32) * VHM_SIZE;
    }

    if (m_paletteChanged)
    {
        updatePalette();
    }

    if (m_svtLoad)
    {
        m_svtLoad = false;
        m_swapChain->WaitForPreviousFrames();
      
        writeSVT(&m_svtBuffer.cpu);

        cmdBuffer.Copy(&m_svtBuffer.cpu, &m_svtBuffer.gpu);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,            
        }, &m_svtBuffer.gpu);


        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM + VHM_SIZE * 4, VHM_SIZE * VHM_ENTRY_SIZE, 0);
        cmdBuffer.Fill(&m_vhmBuffer, m_prevVHM, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_prevVHM + VHM_SIZE * 4, VHM_SIZE * VHM_ENTRY_SIZE, 0);

        cmdBuffer.Barrier({

            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
        }, &m_vhmBuffer);
    }

    bool svtModified = m_svtModified;

    if (m_svtModified)
    {
        m_svtModified = false;
        m_swapChain->WaitForPreviousFrames();

        writeSVT(&m_svtBuffer.cpu);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,          
        }, &m_svtBuffer.gpu);

        cmdBuffer.Copy(&m_svtBuffer.cpu, &m_svtBuffer.gpu);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
        }, &m_svtBuffer.gpu);
    }


    /* Write descriptors */
    {
        DescriptorWriter writer;


        writer.BindSet(frame.descriptorSet);
        {
           // writer.Write(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.perFrameData);
            writer.Write(0, frame.voxelImage, m_sampler);
            writer.Write(1, frame.normalImage, m_sampler);
            writer.Write(2, m_stbnImage, m_sampler);
            writer.Write(3, frame.radianceImage, m_sampler);
            writer.Write(4, frame.specularSunImage, m_sampler);
            writer.Write(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_paletteBuffer);

            //writer.Write(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_svtBuffer.gpu);
            //writer.Write(2, frame.primaryImage, m_sampler);
        }
        writer.Update(*m_device);
    }

    #if 0
    {
        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_currentVHM + VHM_SIZE * 4, VHM_SIZE * 16, 0);

        cmdBuffer.Barrier({

            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            
        }, &m_vhmBuffer);
    }
    #else
    {
        cmdBuffer.Barrier({
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            
        }, &m_vhmBuffer);  
    }
    #endif

    /* Primary pass */
    {
        /*
        Attachment mainAttachment = {
            .Image = &frame.primaryImage,
            .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
        };*/

        RenderPass renderPass;
        renderPass.Area = frame.voxelImage.GetArea(),
        renderPass.Attachments = { 
            {
                .Image = &frame.voxelImage,
                .ClearValue = { VkClearColorValue{.uint32 = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX }} },
            },
            {
                .Image = &frame.normalImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },
            {
                .Image = &frame.radianceImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },
            {
                .Image = &frame.specularSunImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },           
        };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_primaryPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_primaryPass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        cmdBuffer.EndRenderPass();
    }

    /* Transition images */
    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &frame.voxelImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cmdBuffer.Barrier(barrier, &frame.normalImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cmdBuffer.Barrier(barrier, &frame.radianceImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cmdBuffer.Barrier(barrier, &frame.specularSunImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

#if 0
    {
        RenderPass renderPass;
        renderPass.Area = frame.blurImage.GetArea(),
        renderPass.Attachments = { 
            {
                .Image = &frame.blurImage,
                .ClearValue = { VkClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f } },
            },         
        };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_blurPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        cmdBuffer.EndRenderPass();        
    }

    {
        MemoryBarrier barrier = {
            .PrevAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        cmdBuffer.Barrier(barrier, &frame.blurImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);        
    }
#endif

#if 1
    //if (m_frameNum % 64 == 0)
    if (svtModified)
    {
        m_gen += 1;
        pc.CurrentGen = m_gen;

        m_swapChain->WaitForPreviousFrames();

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        }, &m_vhmBuffer);

        /* Compute pass */
        
        //vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagingPass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_averagingPass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagingPass);

        vkCmdDispatch(cmdBuffer, VHM_SIZE / 1024, 1, 1);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        }, &m_vhmBuffer);

    }
#endif

#if 1
    if (m_frameNum % 128 == 127)
    {
        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        }, &m_vhmBuffer);

        cmdBuffer.Fill(&m_vhmBuffer, m_prevVHM, VHM_SIZE * 4, 0xffffffff);
        cmdBuffer.Fill(&m_vhmBuffer, m_prevVHM + VHM_SIZE * 4, VHM_SIZE * VHM_ENTRY_SIZE, 0);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .NextStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        }, &m_vhmBuffer);

        vkCmdPushConstants(cmdBuffer, m_purgePip.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_purgePip);

        vkCmdDispatch(cmdBuffer, VHM_SIZE / 1024, 1, 1); 
        
        m_purge = false;
        std::swap(m_currentVHM, m_prevVHM);

        cmdBuffer.Barrier({
            .PrevAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .NextAccess = VK_ACCESS_SHADER_READ_BIT,
            .PrevStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .NextStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        }, &m_vhmBuffer);

        //LOG_INFO("PURGE");
    }
#endif


    /* Composite pass */
    {
        Attachment mainAttachment = {
            .Image = &dstImage,
            .ClearValue = { VkClearColorValue{ m_skyColor.r, m_skyColor.g, m_skyColor.b, 1.0f } },
        };

        RenderPass renderPass;
        renderPass.Area = m_swapChain->GetArea();
        renderPass.Attachments = { mainAttachment };

        cmdBuffer.BeginRenderPass(renderPass);

        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass.Layout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_compositePass.Layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePass);
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        if (1) // !m_camera.active
        {
            if (ImDrawData* drawData = ImGui::GetDrawData())
            {
                ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
            }
        }

        cmdBuffer.EndRenderPass();
    }
}


void MainScene::OnResizeEvent(int width, int height)
{
    createImages(width, height);
}


bool MainScene::OnKeyEvent(int key, int scancode, int action)
{
    auto window = m_app->GetWindow();

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        m_prevCursorPos = getCursorPosition();
        m_camera.active = !m_camera.active;

        if (m_camera.active)
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        else    
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        Vec3u pos = glm::clamp(Vec3i(m_camera.pos), Vec3i(0), Vec3i(m_svt.GetDimensions()) - 1);
        Vec3u size = Vec3u(1);

        /*
        VoxelArray voxels = {
            { 
                Voxel{ 255 }, Voxel{ 254 }, Voxel{ 254 }, Voxel{ 255 }, 
                Voxel{ 254 }, Voxel{ 255 }, Voxel{ 255 }, Voxel{ 254 },  
            },
            { 2, 2, 2 },
        };*/

        //VoxelArray voxels = MakeSphere(5, Voxel{ 255 });

        //VoxelArray voxels;
        //loadModel("data/teapot.vox", &voxels);

        if (m_selectedObject)
        {
            auto result = RayTrace(m_svt, m_camera.pos, m_camera.dir);
            if (result.Hit)
            {
                Vec3 pos = Vec3(result.VoxelPos) + result.Normal;
                pos = glm::clamp(pos, Vec3(0.0f), Vec3(m_svt.GetDimensions()) - 1.0f);

                //m_svt.Modify(pos, pos + size, Voxel{255});
                m_svt.Modify(pos, m_selectedObject->Voxels);
                m_svtModified = true;
            }
        }
    }

    if (key == GLFW_KEY_F && action == GLFW_PRESS)
    {
        Vec3u pos = glm::clamp(Vec3i(m_camera.pos), Vec3i(0), Vec3i(m_svt.GetDimensions()) - 1);
        VoxelArray mask = MakeSphere(5, Voxel{ 255 });

        m_svt.Subtract(pos, mask);
        m_svtModified = true;
    }

    if (key == GLFW_KEY_T && action == GLFW_PRESS)
    {
        m_svtLoad = true;
    }

    if (key == GLFW_KEY_P && action == GLFW_PRESS)
    {
        m_purge = true;
    }

    if (false)
    {
        //Vec3u pos = glm::min(Vec3u(glm::max(m_camera.pos, Vec3(0))), m_svt.GetDimensions());

        Vec3u pos = Vec3u(341, 206, 201);
        Vec3u size = Vec3u(2);

        m_svt.Modify(pos, pos + size, Voxel{203});
        //LOG_TRACE("Voxels = {}", m_svt.GetVoxels().size());
        m_swapChain->WaitForPreviousFrames();

        writeSVT(&m_svtBuffer.cpu);

        m_device->Submit([&](CommandBuffer cmdBuffer)
        {
            /*
            cmdBuffer.Barrier({
                .PrevAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .NextAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
                .NextStage = VK_PIPELINE_STAGE_TRANSFER_BIT,          
            }, &m_svtBuffer.gpu);*/

            cmdBuffer.Copy(&m_svtBuffer.cpu, &m_svtBuffer.gpu);
/*
            cmdBuffer.Barrier({
                .PrevAccess = VK_ACCESS_TRANSFER_WRITE_BIT,
                .NextAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .PrevStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .NextStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,            
            }, &m_svtBuffer.gpu);*/
        });
    }

    return false;
}


void MainScene::OnMouseMovedEvent(Vec2 pos)
{
    if (m_camera.active)
    {
        Vec2 movement = pos - m_prevCursorPos;
        m_prevCursorPos = pos;

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


bool MainScene::OnMouseButtonEvent(int button, int action)
{

    if (!m_camera.active)
        return false;

    if (action == GLFW_PRESS)
    {
        if (button == GLFW_MOUSE_BUTTON_3)
        {
            auto result = RayTrace(m_svt, m_camera.pos, m_camera.dir);
            if (!result.Hit)
                return false;

            LOG_TRACE("Voxel = {}", result.VoxelPtr);
            LOG_TRACE("Voxel pos = {}", result.VoxelPos);
            LOG_TRACE("Position = {}", result.Pos);
            LOG_TRACE("Normal = {}", result.Normal);
            LOG_TRACE("Material = {}", m_svt.GetVoxels()[result.VoxelPtr].idx);
        }
        else
        {
            handleMouseClick(button == GLFW_MOUSE_BUTTON_1, button == GLFW_MOUSE_BUTTON_2);
            m_mouseTimer = 0.5f;    // initial delay before `handleMouseClick` can be called again.
            m_mouseButtonPressed = true;
        }
    }
    else if (action == GLFW_RELEASE)
    {
        m_mouseButtonPressed = false;
    }

    /*
    if (action == GLFW_PRESS)
    {
        auto result = RayTrace(m_svt, m_camera.pos, m_camera.dir);
        if (!result.Hit)
            return false;

        if (button == GLFW_MOUSE_BUTTON_1)
        {
            LOG_TRACE("===============================================");
            LOG_TRACE("Voxel = {}", result.VoxelPtr);
            LOG_TRACE("Voxel pos = {}", result.VoxelPos);
            LOG_TRACE("Position = {}", result.Pos);
            LOG_TRACE("Normal = {}", result.Normal);
            LOG_TRACE("Material = {}", m_svt.GetVoxels()[result.VoxelPtr].idx);

            Vec3u pos = glm::clamp(result.VoxelPos, Vec3u(0), Vec3u(m_svt.GetDimensions() - 1u));
            if (1)
            {
                VoxelArray mask = { { Voxel{1} }, Vec3u(1) };
                m_svt.Subtract(pos, mask);
            }
            else
            {
                VoxelArray mask = MakeSphere(5, Voxel{ 255 });
                Vec3u min = pos - mask.Size / 2u;
                m_svt.Subtract(min, mask);
            }
            m_svtModified = true;
        }
        else if (button == GLFW_MOUSE_BUTTON_2)
        {
            Vec3 pos = Vec3(result.VoxelPos) + result.Normal;
            pos = glm::clamp(pos, Vec3(0), Vec3(m_svt.GetDimensions() - 1u));

            if (1)
            {
                Vec3u min = Vec3u(pos);
                m_svt.Modify(min, min + Vec3u(1), Voxel{ u8(m_selectedMaterial) });
            }
            else
            {
                VoxelArray voxels = MakeSphere(5, Voxel{ 255 });
                Vec3u min = Vec3u(pos) - voxels.Size / 2u;
                m_svt.Modify(min, voxels);
            }
            m_svtModified = true;            
        }
    }*/

    return false;
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
            }

            if (path == "data/nuke")
            {
                m_palette[216].emission = 10.0f;
            }
            if (path == "data/castle2")
            {
                //m_palette[1].emission = 100.0f;
            }
            if (path == "data/sponza")
            {
                m_palette[255].color[0] = m_palette[255].color[1] = m_palette[255].color[2] = m_palette[255].color[3] = 255;
                m_palette[255].emission = 50.0f;
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

inline float ColorDistance(Vec3 a, Vec3 b)
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


void MainScene::writeSVT(Buffer *svtBuffer)
{
    const size_t dataSize = 
        sizeof(SVNode) * m_svt.GetNodes().size() + 
        sizeof(Voxel) * m_svt.GetVoxels().size();

    static std::vector<byte> data;
    data.resize(dataSize);

    auto srcNodes = m_svt.GetNodes();
    auto srcVoxels = m_svt.GetVoxels();

    SVNode *dstNodes = (SVNode*)&data[0];
    Voxel *dstVoxels = (Voxel*)(dstNodes + srcNodes.size());

    memcpy(dstNodes, srcNodes.data(), sizeof(SVNode) * srcNodes.size());
    memcpy(dstVoxels, srcVoxels.data(), sizeof(Voxel) * srcVoxels.size());

    m_device->Write(svtBuffer, 0, &data[0], dataSize);
}


void MainScene::createImages(int width, int height)
{
    for (Frame& frame : m_frames)
    {
        auto size = m_swapChain->GetSize();
        if (width == 0 || height == 0)
            return;
        
        /*
        frame.primaryImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_B8G8R8A8_UNORM,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });*/

        frame.voxelImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_R32G32B32A32_UINT,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });

        frame.normalImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_B8G8R8A8_UNORM,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });

        frame.radianceImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });

        frame.specularSunImage = m_device->CreateImage({
            .Type = VK_IMAGE_TYPE_2D,
            .Format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .Size = { (uint32_t)width, (uint32_t)height, 1 },
            .MipLevels = 1,
            .ViewType = VK_IMAGE_VIEW_TYPE_2D,
        });
    }
}


void MainScene::updatePalette()
{
    m_device->UploadBuffer(&m_paletteBuffer, 0, m_palette.data(), m_palette.size() * sizeof(m_palette[0]));
}


VkShaderModule MainScene::loadShader(std::string_view path)
{
    auto code = FileSystem::ReadFile(path);
    if (!code)
    {
        LOG_ERROR("Failed to load shader: '{}'", path);
        return VK_NULL_HANDLE;
    }

    return m_device->CreateShaderModule(*code);
}


void MainScene::initImGui()
{
    auto window = m_app->GetWindow();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        LOG_ERROR("Failed to initialize imgui for glfw");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    //io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen7x14b.ttf", 14.0f);
    io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen8x16b.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("data/fonts/Tamzen10x20b.ttf", 20.0f);

    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(*m_device, &poolInfo, nullptr, &imguiPool));

    VkFormat swapChainFormat = m_app->GetSwapChain().GetImageFormat();

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = m_device->GetInstance();
    info.PhysicalDevice = m_device->GetAdapter();
    info.Device = static_cast<VkDevice>(*m_device);
    info.QueueFamily = m_device->GetGraphicsQueueFamily();
    info.Queue = m_device->GetGraphicsQueue();
    info.DescriptorPool = imguiPool;
    info.MinImageCount = 3;
    info.ImageCount = 3;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainFormat;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.UseDynamicRendering = true;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        LOG_ERROR("Failed to initialize imgui for vulkan");
        return;
    }
}


void MainScene::deinitImGui()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}


VoxelArray MakeSphere(int radius, Voxel color)
{
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