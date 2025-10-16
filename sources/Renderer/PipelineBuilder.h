#pragma once
#include <vulkan/vulkan.h>

#include <vector>
#include <span>



struct ColorAttachment
{
    VkFormat Format;
    VkPipelineColorBlendAttachmentState BlendState;

    ColorAttachment();
    ColorAttachment(VkFormat format);
    ColorAttachment(VkFormat format, const VkPipelineColorBlendAttachmentState& blendState);
};



class PipelineBuilder
{
public:
    PipelineBuilder();

    void Reset();
    VkPipeline Build(VkDevice device);

    // Shader stages
    void AddShader(VkShaderStageFlagBits stage, VkShaderModule module, const char *entryPoint="main");
    void AddVertexShader(VkShaderModule module, const char *entryPoint="main");
    void AddFragmentShader(VkShaderModule module, const char *entryPoint="main");

    // Vertex Input
    void AddVertexBinding(const VkVertexInputBindingDescription& binding);
    void AddVertexBinding(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate=VK_VERTEX_INPUT_RATE_VERTEX);
    void AddVertexAttribute(const VkVertexInputAttributeDescription& attribute);
    void AddVertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);

    // Input Assembly
    void SetInputTopology(VkPrimitiveTopology topology, bool primitiveRestart=false);

    // Rasterizer
    void SetPolygonMode(VkPolygonMode polygonMode);
    void SetCullMode(VkCullModeFlags cullMode);
    void SetFrontFace(VkFrontFace frontFace);
    void EnableDepthBias(float constant, float clamp, float slopeFactor);

    // Color Blend
    void SetFramebufferLogicOp(VkLogicOp logicOp);
    void SetBlendConstants(float blendConstants[4]);

    // Rendering
    void SetColorAttachments(std::initializer_list<ColorAttachment> attachments);
    void SetDepthAttachment(VkFormat format);
    void SetStencilAttachment(VkFormat format);

    // Layout
    void SetDescriptorLayouts(std::span<VkDescriptorSetLayout> layouts);
    void SetPushConstantRanges(std::span<VkPushConstantRange> ranges);


private:
    VkPipelineInputAssemblyStateCreateInfo  m_inputAssembly{};
    VkPipelineRasterizationStateCreateInfo  m_rasterizer{};
    VkPipelineMultisampleStateCreateInfo    m_multisample{};
    VkPipelineDepthStencilStateCreateInfo   m_depthStencil{};
    VkPipelineColorBlendStateCreateInfo     m_colorBlend{};
    VkPipelineRenderingCreateInfo           m_rendering{};
    VkPipelineLayoutCreateInfo              m_layoutInfo{};

    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
    std::vector<VkVertexInputBindingDescription> m_vertexBindings;
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    std::vector<VkPipelineColorBlendAttachmentState> m_colorAttachmentStates;
    std::vector<VkFormat> m_colorAttachmentFormats;
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
};
