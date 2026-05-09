#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>

// =================================================================================
// 1. DescriptorAllocator (动态描述符池大管家)
// 职责：自动管理 Pool 的创建和扩容。如果一个 Pool 满了，自动开一个新的。
// =================================================================================
class DescriptorAllocator 
{
public:
    void init(VkDevice device);
    void cleanup();

    // 🌟 核心魔法：每帧开始前调用，瞬间回收上一帧产生的所有描述符！
    void resetPools();

    // 动态分配 Set
    bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);

private:
    VkDevice device;
    VkDescriptorPool currentPool{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> usedPools;
    std::vector<VkDescriptorPool> freePools;

    VkDescriptorPool grabPool();
    friend class DescriptorBuilder;
};

// =================================================================================
// 2. DescriptorBuilder (描述符装配工/建造者模式)
// 职责：提供清爽的链式 API，快速绑定 Buffer 和 Image。
// =================================================================================
class DescriptorBuilder 
{
public:
    static DescriptorBuilder begin(DescriptorAllocator* allocator, VkDescriptorSetLayout layout);

    DescriptorBuilder& bindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo);
    DescriptorBuilder& bindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo);

    bool build(VkDescriptorSet& set);

private:
    DescriptorBuilder(DescriptorAllocator* allocator, VkDescriptorSetLayout layout);

    DescriptorAllocator* allocator;
    VkDescriptorSetLayout layout;
    std::vector<VkWriteDescriptorSet> writes;
};