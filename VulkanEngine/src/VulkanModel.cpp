#include "VulkanModel.hpp"
#include "VulkanDevice.hpp"
#include <stdexcept>
#include <cstring>
#include <stdexcept>

VulkanModel::VulkanModel(VulkanDevice& device, const std::vector<Vertex>& vertices)
    : device(device) {
    createVertexBuffers(vertices);
}

VulkanModel::~VulkanModel() {
    // 架构师铁律：自己申请的显存，含着泪也要自己释放
    vkDestroyBuffer(device.getDevice(), vertexBuffer, nullptr);
    vkFreeMemory(device.getDevice(), vertexBufferMemory, nullptr);
}

void VulkanModel::createVertexBuffers(const std::vector<Vertex>& vertices) {
    vertexCount = static_cast<uint32_t>(vertices.size());
    // 算一下三个顶点总共占多少字节内存
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;

    // 1. 创建逻辑 Buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; // 说明它是用来存顶点的
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    // 2. 查询这个逻辑 Buffer 到底需要多少物理内存，以及它支持放在哪种内存条上
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device.getDevice(), vertexBuffer, &memRequirements);

    // 3. 申请真实的物理内存
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    // 请求 CPU 可见 (HOST_VISIBLE) 且 内存一致 (HOST_COHERENT) 的显存
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    // 4. 将逻辑 Buffer 与物理内存绑定 (偏移量为 0)
    vkBindBufferMemory(device.getDevice(), vertexBuffer, vertexBufferMemory, 0);

    // 5. 暴力装填：把 C++ 的 vector 数据强行塞进显存！
    void* data;
    vkMapMemory(device.getDevice(), vertexBufferMemory, 0, bufferSize, 0, &data); // 打开显存盖子
    memcpy(data, vertices.data(), (size_t)bufferSize);                            // 倒进去
    vkUnmapMemory(device.getDevice(), vertexBufferMemory);                        // 盖上盖子
}

void VulkanModel::bind(VkCommandBuffer commandBuffer) {
    VkBuffer buffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
}

void VulkanModel::draw(VkCommandBuffer commandBuffer) {
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}