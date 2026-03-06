#include "VulkanModel.hpp"
#include "VulkanDevice.hpp"
#include <stdexcept>
#include <cstring>
#include <stdexcept>
#include <iostream>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

VulkanModel::VulkanModel(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : device(device)
{
    createVertexBuffers(vertices);
    createIndexBuffers(indices);
}

VulkanModel::VulkanModel(VulkanDevice& device, const VulkanModel::Builder& builder) : device(device) 
{
    createVertexBuffers(builder.vertices);
    createIndexBuffers(builder.indices);
}

VulkanModel::~VulkanModel() 
{
    vkDestroyBuffer(device.getDevice(), vertexBuffer, nullptr);
    vkFreeMemory(device.getDevice(), vertexBufferMemory, nullptr);

    if (hasIndexBuffer) 
    {
        vkDestroyBuffer(device.getDevice(), indexBuffer, nullptr);
        vkFreeMemory(device.getDevice(), indexBufferMemory, nullptr);
    }
}

void VulkanModel::createVertexBuffers(const std::vector<Vertex>& vertices)
{
    vertexCount = static_cast<uint32_t>(vertices.size());
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device.getDevice(), vertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(device.getDevice(), vertexBuffer, vertexBufferMemory, 0);

    void* data;
    vkMapMemory(device.getDevice(), vertexBufferMemory, 0, bufferSize, 0, &data); 
    memcpy(data, vertices.data(), (size_t)bufferSize);                           
    vkUnmapMemory(device.getDevice(), vertexBufferMemory);                       
}

void VulkanModel::bind(VkCommandBuffer commandBuffer) 
{
    VkBuffer buffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

    if (hasIndexBuffer) 
    {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void VulkanModel::draw(VkCommandBuffer commandBuffer) 
{
    if (hasIndexBuffer) 
    {
        vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
    } 
    else 
    {
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    }
}

void VulkanModel::createIndexBuffers(const std::vector<uint32_t>& indices) 
{
    indexCount = static_cast<uint32_t>(indices.size());
    hasIndexBuffer = indexCount > 0;
    if (!hasIndexBuffer) return;

    VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; 
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &indexBuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create index buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device.getDevice(), indexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &indexBufferMemory) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }

    vkBindBufferMemory(device.getDevice(), indexBuffer, indexBufferMemory, 0);

    void* data;
    vkMapMemory(device.getDevice(), indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device.getDevice(), indexBufferMemory);
}

std::unique_ptr<VulkanModel> VulkanModel::createModelFromFile(VulkanDevice& device, const std::string& filepath) 
{
    Builder builder{};
    builder.loadModel(filepath);
    std::cout << "成功加载模型: " << filepath << " 顶点数: " << builder.vertices.size() << " 索引数: " << builder.indices.size() << "\n";
    return std::make_unique<VulkanModel>(device, builder);
}

void VulkanModel::Builder::loadModel(const std::string& filepath) 
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) 
    {
        throw std::runtime_error("模型加载失败: " + warn + err);
    }

    vertices.clear();
    indices.clear();
    std::unordered_map<Vertex, uint32_t> uniqueVertices{}; // 用于去重的哈希表

    for (const auto& shape : shapes) 
    {
        for (const auto& index : shape.mesh.indices) 
        {
            Vertex vertex{};

            // 提取顶点坐标 (OBJ 的数组是铺平的 1D 数组，所以要 * 3)
            vertex.position = 
            {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // 如果有颜色信息，就提取出来；没有的话默认给个白色
            if (attrib.colors.size() > 0) 
            {
                vertex.color = 
                {
                    attrib.colors[3 * index.vertex_index + 0],
                    attrib.colors[3 * index.vertex_index + 1],
                    attrib.colors[3 * index.vertex_index + 2]
                };
            }
            else
            {
                vertex.color = {1.0f, 1.0f, 1.0f}; // 默认白色
            }

            if (attrib.normals.size() > 0 && index.normal_index >= 0) 
            {
                vertex.normal =
                {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } 
            else 
            {
                vertex.normal = {0.0f, 1.0f, 0.0f}; // 默认法线朝上
            }

            if (attrib.texcoords.size() > 0 && index.texcoord_index >= 0) 
            {
                vertex.uv = 
                {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    // Vulkan 的 Y 轴是朝下的，而大多数 3D 建模软件的 Y轴朝上！
                    // 所以必须用 1.0f 减去它来进行垂直翻转，否则贴图是颠倒的！
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1] 
                };
            } 
            else 
            {
                vertex.uv = {0.0f, 0.0f}; // 如果模型没带 UV，给个默认值
            }

            if (uniqueVertices.count(vertex) == 0) 
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
}