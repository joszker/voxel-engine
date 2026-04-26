#include "Buffer.h"


Buffer::Buffer(VmaAllocation alloc, VkBuffer buffer, VkDeviceSize size, VkBufferUsageFlags usage, void *mappedMemory, VkDeviceAddress address)
    : m_alloc(alloc), m_handle(buffer), m_size(size), m_usage(usage), m_mappedMemory(mappedMemory), m_deviceAddress(address)
{
    
}