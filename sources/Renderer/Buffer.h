#pragma once
#include "VkBase.h"
#include <vk_mem_alloc.h>



struct BufferDesc
{
    VkDeviceSize Size;
    VkBufferUsageFlags Usage;

    VmaAllocationCreateFlags AllocFlags = 0;
    VmaMemoryUsage AllocType = VMA_MEMORY_USAGE_AUTO;
    VkMemoryPropertyFlags RequiredFlags = 0;
    VkMemoryPropertyFlags PreferredFlags = 0;
};


class Buffer
{
public:
    Buffer() = default;
    Buffer(VmaAllocation alloc, VkBuffer buffer, VkDeviceSize size, VkBufferUsageFlags usage, void *mappedMemory, VkDeviceAddress address);

    inline operator VkBuffer() const { return m_handle; }
    inline VmaAllocation GetAllocation() const { return m_alloc; }
    inline VkDeviceSize GetSize() const { return m_size; }
    inline VkBufferUsageFlags GetUsage() const { return m_usage; }
    inline VkPipelineStageFlags GetCurrentStage() const { return m_currentStage; }
    inline void *GetMapped() const { return m_mappedMemory; }
    inline VkDeviceAddress GetDeviceAddress() const { return m_deviceAddress; }

private:
    VmaAllocation m_alloc = nullptr;
    VkBuffer m_handle = nullptr;
    VkDeviceSize m_size;
    VkBufferUsageFlags m_usage;
    VkPipelineStageFlags m_currentStage{};
    void *m_mappedMemory = nullptr;     // Only used for persistently mapped buffers (VMA_ALLOCATION_CREATE_MAPPED_BIT).
    VkDeviceAddress m_deviceAddress;

    friend class CommandBuffer;
};



#if 0
struct TransientBuffer
{
    Buffer *deviceBuffer;       // Device local buffer
    Buffer hostBuffer;          // Staging host buffer
};
#endif