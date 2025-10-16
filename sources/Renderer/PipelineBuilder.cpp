#include "PipelineBuilder.h"
#include "VkBase.h"
#include "Base/Log.h"

#include <array>
#include <string_view>


ColorAttachment::ColorAttachment() : Format(VK_FORMAT_UNDEFINED), BlendState({})
{

}

ColorAttachment::ColorAttachment(VkFormat format) : ColorAttachment(format, {})
{
    
}

ColorAttachment::ColorAttachment(VkFormat format, const VkPipelineColorBlendAttachmentState& blendState)
    : Format(format), BlendState(blendState)
{

}


PipelineBuilder::PipelineBuilder()
{
    /* Input Assembly */
    m_inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    /* Rasterization */
    m_rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_rasterizer.depthClampEnable = VK_FALSE;           // Do not clamp fragment depth values
    m_rasterizer.rasterizerDiscardEnable = VK_FALSE;    // Do not discard primitives
    m_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;    
    m_rasterizer.cullMode = VK_CULL_MODE_NONE;
    m_rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    m_rasterizer.depthBiasEnable = VK_FALSE;            // Do not bias fragment depth values
    m_rasterizer.lineWidth = 1.0f;

    /* Disable multisampling by default */
    m_multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_multisample.sampleShadingEnable = VK_FALSE;
    m_multisample.minSampleShading = 1.0f;
    m_multisample.pSampleMask = nullptr; 
    m_multisample.alphaToCoverageEnable = VK_FALSE;
    m_multisample.alphaToOneEnable = VK_FALSE;

    /* Disable depth stencil by default */
    m_depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
#if 0
    m_depthStencil.depthTestEnable = VK_FALSE;
    m_depthStencil.depthWriteEnable = VK_FALSE;
    m_depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
    m_depthStencil.depthBoundsTestEnable = VK_FALSE;
    m_depthStencil.stencilTestEnable = VK_FALSE;
    m_depthStencil.front = {};
    m_depthStencil.back = {};
    m_depthStencil.minDepthBounds = 0.f;
    m_depthStencil.maxDepthBounds = 1.f;
#endif

    /* Color Blending */
    m_colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    /* Rendering Info */
    m_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    m_rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    m_rendering.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    /* Layout Info */
    m_layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
}


void PipelineBuilder::Reset()
{
    *this = PipelineBuilder();
}


VkPipeline PipelineBuilder::Build(VkDevice device)
{
    std::array dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = uint32_t(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();


    VkViewport viewport{};
    VkRect2D scissor{};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    /* Vertex input state */
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = uint32_t(m_vertexBindings.size());
    vertexInput.pVertexBindingDescriptions = m_vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount = uint32_t(m_vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = m_vertexAttributes.data();

    /* Pipeline layout */
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &m_layoutInfo, nullptr, &layout));
    

    /* Create the graphics pipeline */
    VkPipeline result;
    {
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.pNext = &m_rendering;

        info.stageCount = uint32_t(m_shaderStages.size());
        info.pStages = m_shaderStages.data();

        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &m_inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &m_rasterizer;
        info.pMultisampleState = &m_multisample;
        info.pDepthStencilState = NULL;//&m_depthStencil;
        info.pColorBlendState = &m_colorBlend;
        info.pDynamicState = &dynamicState;
        info.layout = layout;
        info.renderPass = nullptr;  // use dynamic rendering

        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result));
    }
    return result;
}


void PipelineBuilder::AddShader(VkShaderStageFlagBits stage, VkShaderModule module, const char *entryPoint)
{
    m_shaderStages.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = entryPoint,
    });
}


void PipelineBuilder::AddVertexShader(VkShaderModule module, const char *entryPoint)
{
    AddShader(VK_SHADER_STAGE_VERTEX_BIT, module, entryPoint);
}


void PipelineBuilder::AddFragmentShader(VkShaderModule module, const char *entryPoint)
{
    AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, module, entryPoint);    
}


void PipelineBuilder::AddVertexBinding(const VkVertexInputBindingDescription& binding)
{
    m_vertexBindings.push_back(binding);
}


void PipelineBuilder::AddVertexBinding(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate)
{
    AddVertexBinding({ binding, stride, inputRate });
}


void PipelineBuilder::AddVertexAttribute(const VkVertexInputAttributeDescription& attribute)
{
    m_vertexAttributes.push_back(attribute);
}


void PipelineBuilder::AddVertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset)
{
    AddVertexAttribute({ location, binding, format, offset });
}


void PipelineBuilder::SetInputTopology(VkPrimitiveTopology topology, bool primitiveRestart)
{
    m_inputAssembly.topology = topology;
    m_inputAssembly.primitiveRestartEnable = primitiveRestart;
}


void PipelineBuilder::SetPolygonMode(VkPolygonMode polygonMode)
{
    m_rasterizer.polygonMode = polygonMode;
}


void PipelineBuilder::SetCullMode(VkCullModeFlags cullMode)
{
    m_rasterizer.cullMode = cullMode;
}


void PipelineBuilder::SetFrontFace(VkFrontFace frontFace)
{
    m_rasterizer.frontFace = frontFace;
}


void PipelineBuilder::EnableDepthBias(float constant, float clamp, float slopeFactor)
{
    m_rasterizer.depthBiasEnable = VK_TRUE;
    m_rasterizer.depthBiasConstantFactor = constant;
    m_rasterizer.depthBiasClamp = clamp;
    m_rasterizer.depthBiasSlopeFactor = slopeFactor;
}


void PipelineBuilder::SetFramebufferLogicOp(VkLogicOp logicOp)
{
    m_colorBlend.logicOpEnable = VK_TRUE;
    m_colorBlend.logicOp = logicOp;
}


void PipelineBuilder::SetBlendConstants(float blendConstants[4])
{
    memcpy(m_colorBlend.blendConstants, blendConstants, 4 * sizeof(float));
}


void PipelineBuilder::SetColorAttachments(std::initializer_list<ColorAttachment> attachments)
{
    m_colorAttachmentStates.resize(attachments.size());
    m_colorAttachmentFormats.resize(attachments.size());

    size_t index = 0;
    for (auto& attachment : attachments)
    {
        m_colorAttachmentStates[index] = attachment.BlendState;
        m_colorAttachmentFormats[index] = attachment.Format;
    }

    m_colorBlend.pAttachments = m_colorAttachmentStates.data();
    m_colorBlend.attachmentCount = m_colorAttachmentStates.size();
    m_rendering.colorAttachmentCount = m_colorAttachmentFormats.size();
    m_rendering.pColorAttachmentFormats = m_colorAttachmentFormats.data();
}


void PipelineBuilder::SetDepthAttachment(VkFormat format)
{
    m_rendering.depthAttachmentFormat = format;
}


void PipelineBuilder::SetStencilAttachment(VkFormat format)
{
    m_rendering.stencilAttachmentFormat = format;
}


void PipelineBuilder::SetDescriptorLayouts(std::span<VkDescriptorSetLayout> layouts)
{
    m_descriptorSetLayouts.assign(layouts.begin(), layouts.end());

    m_layoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
    m_layoutInfo.setLayoutCount = m_descriptorSetLayouts.size();
}


void PipelineBuilder::SetPushConstantRanges(std::span<VkPushConstantRange> ranges)
{
    m_pushConstantRanges.assign(ranges.begin(), ranges.end());

    m_layoutInfo.pPushConstantRanges = m_pushConstantRanges.data();
    m_layoutInfo.pushConstantRangeCount = m_pushConstantRanges.size();
}