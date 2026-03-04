#pragma once
#include <vulkan/vulkan.h>
#include "VulkanModel.hpp"
#include "VulkanGameObject.hpp"

struct GlobalUbo 
{
    glm::mat4 projectionView{1.f}; 
    glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .1f}; 
    glm::vec3 lightDirection = glm::normalize(glm::vec3(1.f, -3.f, -1.f)); 
    alignas(16) glm::vec4 lightColor{1.f}; 
    alignas(16) glm::vec4 cameraPos; 
};

struct SimplePushConstantData 
{
    glm::mat4 modelMatrix{1.f}; 
    glm::mat4 normalMatrix{1.f}; 
};

// 【渲染数据总线】：每一帧流经各大 System 的唯一参数
struct FrameInfo 
{
    uint32_t frameIndex;
    float frameTime;               // 传递当前时间用于动画
    VkCommandBuffer commandBuffer; // 录制命令的笔
    VkDescriptorSet globalDescriptorSet; // 全局户口本 (包含所有贴图和UBO)
    
    std::vector<VulkanGameObject>& gameObjects;
};