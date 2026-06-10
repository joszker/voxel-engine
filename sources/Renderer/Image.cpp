#include "Image.h"


Image::Image(VmaAllocation alloc, VkImage image, VkImageView imageView, const ImageDesc& desc, const VkImageSubresourceRange& subresRange)
    : m_alloc(alloc), m_handle(image), m_view(imageView), m_desc(desc), m_subresourceRange(subresRange)
{
    
}


VkDeviceSize Image::GetBufferSize() const
{
    size_t texelSize = VkUtils::GetTexelSize(m_desc.Format);

    return texelSize * (size_t)GetWidth() * (size_t)GetHeight() * (size_t)GetDepth();
}