#include "CommandBuffer.h"
#include "Buffer.h"
#include "Image.h"





void CommandBuffer::Begin(VkCommandBufferUsageFlags flags)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;

    VK_CHECK(vkBeginCommandBuffer(m_handle, &beginInfo));
}


void CommandBuffer::End()
{
    vkEndCommandBuffer(m_handle);
}


void CommandBuffer::BeginRenderPass(const VkRenderingInfo& renderInfo)
{
    vkCmdBeginRendering(m_handle, &renderInfo);
}


void CommandBuffer::BeginRenderPass(const RenderPass& renderPass)
{
    /* Set viewport & scissor */
    {
        auto& mainAttachment = renderPass.Attachments[0];

        uint32_t width = mainAttachment.Image->GetWidth();
        uint32_t height = mainAttachment.Image->GetHeight();

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { width, height };

        SetViewport(viewport);
        SetScissor(scissor);
    }

    std::vector<VkRenderingAttachmentInfo> attachments;

    for (auto& attachment : renderPass.Attachments)
    {
        attachments.push_back({
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = attachment.Image->GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
            .loadOp = attachment.LoadOp,
            .storeOp = attachment.StoreOp,
            .clearValue = attachment.ClearValue
        });


        VkImageAspectFlags aspect = attachment.Image->GetAspect();
        VkImageLayout newLayout;
        MemoryBarrier barrier;
        barrier.PrevAccess = VK_ACCESS_NONE;

        if (aspect & VK_IMAGE_ASPECT_COLOR_BIT)
        {
            if (attachment.Read)    barrier.NextAccess |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            if (attachment.Write)   barrier.NextAccess |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            barrier.NextStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || (aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            if (attachment.Read)    barrier.NextAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            if (attachment.Write)   barrier.NextAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            barrier.NextStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            barrier.NextStage |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            
            newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        else
        {
            NOT_IMPLEMENTED();
            newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        }

        /* Transition image layout */
        Barrier(barrier, attachment.Image, newLayout);
    }


    VkRenderingInfo renderInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderPass.Area,
        .layerCount = 1,
        .colorAttachmentCount = (uint32_t)attachments.size(),
        .pColorAttachments = attachments.data(),
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };

    vkCmdBeginRendering(m_handle, &renderInfo);
}


void CommandBuffer::EndRenderPass()
{
    vkCmdEndRendering(m_handle);
}


void CommandBuffer::Barrier(const MemoryBarrier& barrier, Buffer* buffer, VkDeviceSize offset, VkDeviceSize size)
{
    const VkBufferMemoryBarrier bufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = barrier.PrevAccess,
        .dstAccessMask = barrier.NextAccess,
        .buffer = *buffer,
        .offset = offset,
        .size = size,
    };

    VkPipelineStageFlags currentStage = buffer->GetCurrentStage();
    if (currentStage == 0)
        currentStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (barrier.PrevStage != 0)
    {
        ASSERT(barrier.PrevStage == currentStage);
    }

    vkCmdPipelineBarrier(
        m_handle,
        currentStage,                       // srcStageMask
        barrier.NextStage,                  // dstStageMask
        0,                                  // dependencyFlags
        0, nullptr,                         // memory barriers
        1, &bufferMemoryBarrier,            // buffer memory barriers
        0, nullptr                          // image memory barriers
    );
    buffer->m_currentStage = barrier.NextStage;
}


/*
void CommandBuffer::Barrier2(const MemoryBarrier& barrier, Buffer* buffer, VkDeviceSize offset, VkDeviceSize size)
{
    const VkBufferMemoryBarrier2 bufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = barrier.PrevStage,
        .srcAccessMask = barrier.PrevAccess,
        .dstStageMask = barrier.NextStage,
        .dstAccessMask = barrier.NextAccess,
        .buffer = *buffer,
        .offset = offset,
        .size = size,
    };

    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &bufferMemoryBarrier,
    };

    vkCmdPipelineBarrier2(m_handle, &dependency);
    buffer->m_currentStage = barrier.NextStage;
}
*/


void CommandBuffer::Barrier(const MemoryBarrier& barrier, Image* image, VkImageLayout newLayout)
{
    const VkImageMemoryBarrier imageMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = barrier.PrevAccess,
        .dstAccessMask = barrier.NextAccess,
        .oldLayout = image->GetCurrentLayout(),
        .newLayout = newLayout,
        .image = *image,
        .subresourceRange = image->GetSubresourceRange(),
    };
    
    VkPipelineStageFlags currentStage = image->GetCurrentStage();
    if (currentStage == 0)
        currentStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (barrier.PrevStage != 0)
    {
        ASSERT(barrier.PrevStage == currentStage);
    }

    vkCmdPipelineBarrier(
        m_handle,
        currentStage,                       // srcStageMask
        barrier.NextStage,                  // dstStageMask
        0,                                  // dependencyFlags
        0, nullptr,                         // memory barriers
        0, nullptr,                         // buffer memory barriers
        1, &imageMemoryBarrier              // image memory barriers
    );
    image->m_currentStage = barrier.NextStage;
    image->m_currentLayout = newLayout;
}


void CommandBuffer::SetViewport(const VkViewport& viewport)
{
    vkCmdSetViewport(m_handle, 0, 1, &viewport);
}


void CommandBuffer::SetScissor(const VkRect2D& scissor)
{
    vkCmdSetScissor(m_handle, 0, 1, &scissor);
}


void CommandBuffer::Copy(const Buffer *src, Buffer *dst)
{
    Copy(src, dst, std::min(src->GetSize(), dst->GetSize()));
}


void CommandBuffer::Copy(const Buffer *src, Buffer *dst, size_t numBytes)
{
    VkBufferCopy copy = { 
        .srcOffset = 0, 
        .dstOffset = 0, 
        .size = numBytes,
    };
    Copy(src, dst, copy);
}


void CommandBuffer::Copy(const Buffer *src, Buffer *dst, VkBufferCopy copy)
{
    ASSERT((src->GetUsage() & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0);
    ASSERT((dst->GetUsage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0);

    ASSERT(copy.size <= src->GetSize());
    ASSERT(copy.size <= dst->GetSize());

    vkCmdCopyBuffer(m_handle, *src, *dst, 1, &copy);
    dst->m_currentStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
}


void CommandBuffer::Fill(Buffer *dst, VkDeviceSize offset, VkDeviceSize size, u32 data)
{
    vkCmdFillBuffer(m_handle, *dst, offset, size, data);

    dst->m_currentStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
}