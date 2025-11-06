#pragma once
#include "VkBase.h"


class Buffer;
class Image;



struct DescriptorLayout
{
    VkDescriptorSetLayout Handle = VK_NULL_HANDLE;
    std::vector<VkDescriptorPoolSize> Sizes;

    inline operator VkDescriptorSetLayout() { return Handle; }
};


struct DescriptorLayoutBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings;

    void Reset();
    void Add(uint32_t binding, VkDescriptorType type, uint32_t arrayCount = 1);
    DescriptorLayout Build(VkDevice device, VkShaderStageFlags stages = VK_SHADER_STAGE_ALL_GRAPHICS, VkDescriptorSetLayoutCreateFlags flags = 0);
};


struct DescriptorAllocator
{
    struct Allocation
    {
        DescriptorLayout *Layout;
        VkDescriptorSet *Set;
    };
    std::vector<Allocation> Allocations;
    std::vector<VkDescriptorPoolSize> Sizes;

    void Add(DescriptorLayout* layout, VkDescriptorSet *set);
    VkDescriptorPool Allocate(VkDevice device);
};



struct DescriptorWriter
{
    std::vector<VkWriteDescriptorSet> Writes;
    std::vector<VkDescriptorBufferInfo> Buffers;
    std::vector<VkDescriptorImageInfo> Images;
    VkDescriptorSet CurrentSet;

    void BindSet(VkDescriptorSet set);

    void Write(uint32_t binding, VkDescriptorType type, const Buffer& buffer, size_t size = VK_WHOLE_SIZE, size_t offset = 0);
    void Write(uint32_t binding, const Image& image, VkSampler sampler, VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    void Write(uint32_t binding, VkDescriptorType type, const Image& image, VkImageLayout accessLayout);

    void Reset();
    void Update(VkDevice device);
};
