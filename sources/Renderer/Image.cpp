#include "Image.h"


Image::Image(VmaAllocation alloc, VkImage image, VkImageView imageView, const ImageDesc& desc, const VkImageSubresourceRange& subresRange)
    : m_alloc(alloc), m_handle(image), m_view(imageView), m_desc(desc), m_subresourceRange(subresRange)
{
    
}