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



struct Pipeline
{
    VkPipeline Handle;
    VkPipelineLayout Layout;

    inline operator VkPipeline() { return Handle; }
};


/* Helper for creating graphics pipelines */
class PipelineBuilder
{
public:
    PipelineBuilder();

    void Reset();
    Pipeline Build(VkDevice device);

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
    void AddColorAttachment(const ColorAttachment& attachment);
    void SetDepthAttachment(VkFormat format);
    void SetStencilAttachment(VkFormat format);

    // Layout
    void SetDescriptorLayouts(std::span<VkDescriptorSetLayout> layouts);
    void AddDescriptorLayout(VkDescriptorSetLayout layout);
    void SetPushConstantRanges(std::span<VkPushConstantRange> ranges);
    void AddPushConstantRange(VkPushConstantRange range);


private:
    VkPipelineInputAssemblyStateCreateInfo  m_inputAssembly{};
    VkPipelineRasterizationStateCreateInfo  m_rasterizer{};
    VkPipelineMultisampleStateCreateInfo    m_multisample{};
    VkPipelineDepthStencilStateCreateInfo   m_depthStencil{};
    VkPipelineColorBlendStateCreateInfo     m_colorBlend{};
    VkPipelineRenderingCreateInfo           m_rendering{};

    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
    std::vector<VkVertexInputBindingDescription> m_vertexBindings;
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    std::vector<VkPipelineColorBlendAttachmentState> m_colorAttachmentStates;
    std::vector<VkFormat> m_colorAttachmentFormats;
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
};



struct ComputePipelineDesc
{
    VkPipelineCreateFlags Flags = 0;
    VkPipelineShaderStageCreateFlags ShaderFlags = 0;
    VkShaderModule ShaderModule = 0;
    const char *ShaderEntryPoint = "main";
    std::span<VkDescriptorSetLayout> DescriptorLayouts;
    std::span<VkPushConstantRange> PushConstantRanges;
};


Pipeline CreateComputePipeline(VkDevice device, const ComputePipelineDesc& desc);