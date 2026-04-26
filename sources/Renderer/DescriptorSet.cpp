#include "DescriptorSet.h"

#include "Buffer.h"
#include "Image.h"


void DescriptorLayoutBuilder::Reset()
{
    Bindings.clear();
}

void DescriptorLayoutBuilder::Add(uint32_t binding, VkDescriptorType type, uint32_t arrayCount)
{
    Bindings.push_back({
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = arrayCount,
        .stageFlags = 0,
    });
}

DescriptorLayout DescriptorLayoutBuilder::Build(VkDevice device, VkShaderStageFlags stages, VkDescriptorSetLayoutCreateFlags flags)
{
    DescriptorLayout layout;

    for (auto& binding : Bindings)
    {
        binding.stageFlags = stages;

        layout.Sizes.push_back({ binding.descriptorType, binding.descriptorCount });
    }

    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = flags,
        .bindingCount = (uint32_t)Bindings.size(),
        .pBindings = Bindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout.Handle));
    return std::move(layout);
}


void DescriptorAllocator::Add(const DescriptorLayout* layout, VkDescriptorSet *set)
{
    Sizes.insert(Sizes.end(), layout->Sizes.begin(), layout->Sizes.end());

    Allocations.push_back({ layout, set });
}


VkDescriptorPool DescriptorAllocator::Allocate(VkDevice device)
{
    VkDescriptorPool pool;
    {
        VkDescriptorPoolCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = 0,
            .maxSets = (uint32_t)Allocations.size(),
            .poolSizeCount = (uint32_t)Sizes.size(),
            .pPoolSizes = Sizes.data(),
        };

        VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &pool));
    }

    for (auto& alloc : Allocations)
    {
        VkDescriptorSetAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &alloc.Layout->Handle,
        };

        VK_CHECK(vkAllocateDescriptorSets(device, &info, alloc.Set));
    }

#if 0
    for (auto& alloc : Allocations)
    {
        if (alloc.Layout->Handle)
        {
            vkDestroyDescriptorSetLayout(device, alloc.Layout->Handle, nullptr);
            alloc.Layout->Handle = VK_NULL_HANDLE;
        }
    }
#endif

    return pool;
}


void DescriptorWriter::BindSet(VkDescriptorSet set)
{
    CurrentSet = set;
}


void DescriptorWriter::Write(uint32_t binding, VkDescriptorType type, const Buffer& buffer, size_t size, size_t offset)
{
    Resources.push_back({ 
        .Buffer = {
            .buffer = buffer,
            .offset = offset,
            .range = size,
        }
    });

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = CurrentSet,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = type,
        .pBufferInfo = nullptr,     // NOTE: will be assigned later
    };

    Writes.push_back(write);
}


void DescriptorWriter::Write(uint32_t binding, const Image& image, VkSampler sampler, VkImageLayout readLayout)
{
    Resources.push_back({
        .Image = {
            .sampler = sampler,
            .imageView = image.GetView(),
            .imageLayout = readLayout,      // layout that the image subresource will be in when the descriptor is accessed. 
        },
    });

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = CurrentSet,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = nullptr,      // NOTE: will be assigned later
    };

    Writes.push_back(write);
}


void DescriptorWriter::Write(uint32_t binding, VkDescriptorType type, const Image& image, VkImageLayout accessLayout)
{
    ASSERT(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    Resources.push_back({
        .Image = {
            .sampler = nullptr,
            .imageView = image.GetView(),
            .imageLayout = accessLayout,    // layout that the image subresource will be in when the descriptor is accessed. 
        },
    });

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = CurrentSet,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = nullptr,      // NOTE: will be assigned later
    };

    Writes.push_back(write);    
}


void DescriptorWriter::Reset()
{
    Writes.clear();
    Resources.clear();
}


void DescriptorWriter::Update(VkDevice device)
{
    ASSERT(Writes.size() == Resources.size());

    // Assign resource infos before updating
    for (size_t i = 0; i < Writes.size(); i++)
    {
        Writes[i].pBufferInfo = &Resources[i].Buffer;
        Writes[i].pImageInfo = &Resources[i].Image;
    }

    vkUpdateDescriptorSets(device, (uint32_t)Writes.size(), Writes.data(), 0, nullptr);
}