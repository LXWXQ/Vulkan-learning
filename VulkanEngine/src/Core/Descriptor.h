#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>

// =================================================================================
// 【历史遗留 / 已不在渲染路径上】
//
// 这个文件是早期（A 阶段之前）的描述符管理方案：按交换链图像分配多个
// VkDescriptorPool，每帧整体 reset。D-1(b) 之后所有渲染都走
// DescriptorManager（预分配 + 槽位回收，见 DescriptorManager.h）。
//
// 之所以没删：一是还有零散引用，二是它演示了"多池轮转"的另一种思路，
// 将来做 D-4 多帧并行时可能有参考价值。新代码不要再往里加东西。
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