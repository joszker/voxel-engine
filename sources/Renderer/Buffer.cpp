#include "Buffer.h"


Buffer::Buffer(VmaAllocation alloc, VkBuffer buffer, void *mappedMemory)
    : m_alloc(alloc), m_handle(buffer), m_mappedMemory(mappedMemory)
{

}