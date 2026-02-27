#pragma once

#include <vulkan/vulkan.h>
#include "VulkanDevice.hpp"
// 引入 GLM，强制使用弧度制，强制内存对齐
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <vector>

struct Vertex {
    glm::vec2 position; // 位置 (x, y)
    glm::vec3 color;    // 颜色 (r, g, b)

    // --- 核心考点：向管线提供“数据说明书” ---
    
    // 1. 绑定描述 (Binding Description)：告诉 GPU 整个结构体有多大，数据是一次读取一个顶点，还是按实例读取。
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // 对应 Shader 里的 binding = 0
        bindingDescription.stride = sizeof(Vertex); // 步长：跳到下一个顶点需要跨越多少字节
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // 逐顶点读取
        return bindingDescription;
    }

    // 2. 属性描述 (Attribute Descriptions)：告诉 GPU 结构体里面的 position 和 color 具体在哪个位置。
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);

        // Location 0: Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0; // 对应 Shader 里的 layout(location = 0)
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT; // vec2 其实就是两个 32 位浮点数
        attributeDescriptions[0].offset = offsetof(Vertex, position); // 偏移量：0

        // Location 1: Color
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1; // 对应 Shader 里的 layout(location = 1)
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3 是三个 32 位浮点数
        attributeDescriptions[1].offset = offsetof(Vertex, color); // 偏移量：跳过 position 占用的字节数

        return attributeDescriptions;
    }
};

class VulkanModel {
public:
    VulkanModel(VulkanDevice& device, const std::vector<Vertex>& vertices);
    ~VulkanModel();

    VulkanModel(const VulkanModel&) = delete;
    VulkanModel& operator=(const VulkanModel&) = delete;

    void bind(VkCommandBuffer commandBuffer);
    void draw(VkCommandBuffer commandBuffer);

private:
    void createVertexBuffers(const std::vector<Vertex>& vertices);

    VulkanDevice& device;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount;
};