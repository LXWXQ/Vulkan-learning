#pragma once
#include <vulkan/vulkan.h>
#include "VulkanModel.hpp"
#include "VulkanGameObject.hpp"
#define MAX_POINT_LIGHTS 100

struct PointLight {
    glm::vec4 position{}; // xyz = 世界坐标, w = 光照半径 (Radius)
    glm::vec4 color{};    // xyz = 颜色, w = 光照强度 (Intensity)
};
struct GlobalUbo {
    glm::mat4 projectionView{1.f};
    
    alignas(16) glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .05f};
    alignas(16) glm::vec4 lightDirection{1.f, 1.f, 1.f, 0.f}; 
    alignas(16) glm::vec4 lightColor{1.f, 1.f, 1.f, 1.f}; // 这里的 w 就是太阳光强度，保持在 1.0 左右     
    alignas(16) glm::vec4 cameraPos{0.f};                     
    
    alignas(16) int numLights = 0; 
    
    // 🚨 极其关键：C++ 的数组大小必须与 Shader 里的 [100] 严格一致！
    alignas(16) PointLight pointLights[MAX_POINT_LIGHTS];     
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