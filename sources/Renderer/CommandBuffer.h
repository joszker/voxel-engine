#pragma once
#include "VkBase.h"



class Buffer;
class Image;



struct Attachment 
{
    Image* Image{};
    VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue ClearValue{};
    bool Read = false;
    bool Write = true;
};

struct RenderPass
{
    VkRect2D Area = { 0, 0, 0, 0 };
    std::vector<Attachment> Attachments;
    Attachment* DepthAttachment = nullptr;
    Attachment* StencilAttachment = nullptr;
};


struct MemoryBarrier
{
    // Which kinds of operations must finish before the barrier.
    VkAccessFlags PrevAccess = VK_ACCESS_MEMORY_WRITE_BIT;  
    
    // Which kinds of operations will access the memory after the barrier.
    VkAccessFlags NextAccess{};

    // Which kinds of pipeline stages has accessed the memory (optional).
    VkPipelineStageFlags PrevStage{};
    
    // Which kinds of pipeline stages will access the memory.
    VkPipelineStageFlags NextStage{};
};



class CommandBuffer
{
public:
    CommandBuffer() = default;
    CommandBuffer(VkCommandBuffer cmdBuffer) : m_handle(cmdBuffer) {}

    inline operator VkCommandBuffer() { return m_handle; }
    inline VkCommandBuffer *Ptr() { return &m_handle; }

    void Begin(VkCommandBufferUsageFlags flags = 0);
    void End();
    void BeginRenderPass(const VkRenderingInfo& renderInfo);
    void BeginRenderPass(const RenderPass& renderPass);
    void EndRenderPass();

    void Barrier(const MemoryBarrier& barrier, Buffer* buffer, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void Barrier(const MemoryBarrier& barrier, Image* image, VkImageLayout newLayout);

    void SetViewport(const VkViewport& viewport);
    void SetScissor(const VkRect2D& scissor);

    void Copy(const Buffer *src, Buffer *dst);
    void Copy(const Buffer *src, Buffer *dst, size_t numBytes);
    void Fill(Buffer *dst, VkDeviceSize offset, VkDeviceSize size, u32 data);

private:
    VkCommandBuffer m_handle{};
};