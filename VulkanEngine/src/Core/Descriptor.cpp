#include "Descriptor.h"
#include <stdexcept>

// ======================= DescriptorAllocator 实现 =======================

void DescriptorAllocator::init(VkDevice logicalDevice) {
    device = logicalDevice;
}

void DescriptorAllocator::cleanup() {
    for (auto p : freePools) vkDestroyDescriptorPool(device, p, nullptr);
    for (auto p : usedPools) vkDestroyDescriptorPool(device, p, nullptr);
    if (currentPool) vkDestroyDescriptorPool(device, currentPool, nullptr);
}

VkDescriptorPool DescriptorAllocator::grabPool() {
    if (!freePools.empty()) {
        VkDescriptorPool pool = freePools.back();
        freePools.pop_back();
        return pool;
    }
    // 默认分配比例，涵盖常用的类型
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0; // 不使用 FREE_DESCRIPTOR_SET_BIT，我们只整体 Reset
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool newPool;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool) != VK_SUCCESS) {
        throw std::runtime_error("Sentinel: 无法开辟新的描述符池！");
    }
    return newPool;
}

void DescriptorAllocator::resetPools() {
    if (currentPool) {
        usedPools.push_back(currentPool);
        currentPool = VK_NULL_HANDLE;
    }
    for (auto pool : usedPools) {
        vkResetDescriptorPool(device, pool, 0); // 瞬间释放所有 Set
        freePools.push_back(pool);
    }
    usedPools.clear();
}

bool DescriptorAllocator::allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout) {
    if (currentPool == VK_NULL_HANDLE) {
        currentPool = grabPool();
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = currentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, set);

    // 如果当前池子满了，抓一个新的池子重试一次
    if (result == VK_ERROR_FRAGMENTED_POOL || result == VK_ERROR_OUT_OF_POOL_MEMORY) {
        usedPools.push_back(currentPool);
        currentPool = grabPool();
        allocInfo.descriptorPool = currentPool;
        result = vkAllocateDescriptorSets(device, &allocInfo, set);
    }

    return result == VK_SUCCESS;
}

// ======================= DescriptorBuilder 实现 =======================

DescriptorBuilder::DescriptorBuilder(DescriptorAllocator* allocator, VkDescriptorSetLayout layout) 
    : allocator(allocator), layout(layout) {}

DescriptorBuilder DescriptorBuilder::begin(DescriptorAllocator* allocator, VkDescriptorSetLayout layout) {
    return DescriptorBuilder(allocator, layout);
}

DescriptorBuilder& DescriptorBuilder::bindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = bufferInfo;
    writes.push_back(write);
    return *this;
}

DescriptorBuilder& DescriptorBuilder::bindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = imageInfo;
    writes.push_back(write);
    return *this;
}

bool DescriptorBuilder::build(VkDescriptorSet& set) {
    // 1. 从大管家那里申请一个 Set
    if (!allocator->allocate(&set, layout)) {
        return false;
    }
    // 2. 将装配好的目标 Set 赋值给 Writes
    for (auto& w : writes) {
        w.dstSet = set;
    }
    // 3. 一次性更新到 GPU
    vkUpdateDescriptorSets(allocator->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}