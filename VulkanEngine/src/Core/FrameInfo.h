#pragma once
#include <vulkan/vulkan.h>
#include "Scene/Model.h"
#include "Scene/GameObject.h"
#include "Core/Descriptor.h"
#define MAX_POINT_LIGHTS 100
#define WIDTH 1920
#define HEIGHT 1080


struct PointLight 
{
    glm::vec4 position{};
    glm::vec4 color{};
};

struct GlobalUbo 
{
    glm::mat4 projectionView{1.f};
    
    alignas(16) glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .05f};
    alignas(16) glm::vec4 lightDirection{1.f, 1.f, 1.f, 0.f}; 
    alignas(16) glm::vec4 lightColor{1.f, 1.f, 1.f, 1.f};  
    alignas(16) glm::vec4 cameraPos{0.f};                     
    
    alignas(16) int numLights = 0; 
    alignas(16) PointLight pointLights[MAX_POINT_LIGHTS];     
};

struct SimplePushConstantData 
{
    glm::mat4 modelMatrix{1.f}; 
    glm::mat4 normalMatrix{1.f}; 
};

// 引擎运行时的可调参数
struct EngineSettings {
    // SSAO 控制
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    int ssaoKernelSize = 16;
    
    // PBR 与光照控制
    glm::vec4 directionalLightDir{-1.0f, -1.0f, 1.0f, 0.0f};
    glm::vec4 directionalLightColor{1.0f, 1.0f, 1.0f, 1.0f};
    float exposure = 1.0f; // 用于色调映射的曝光值
};

// 渲染统计遥测数据
struct RenderTelemetry {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;

    // 每帧开始前重置清空
    void reset() {
        drawCalls = 0;
        triangles = 0;
        vertices = 0;
    }
};

struct FrameInfo 
{
    uint32_t frameIndex;
    float frameTime;              
    VkCommandBuffer commandBuffer;
    DescriptorAllocator* allocator; // 🌟 动态大管家
    EngineSettings& settings;     // 🌟 指向全局设置的引用
    RenderTelemetry& telemetry;   // 🌟 指向当前帧遥测数据的引用
    std::vector<GameObject>& gameObjects;

    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;

    // ==========================================
    // 🌟 帧数据总线 (Frame Data Bus)
    // ==========================================
    VkBuffer globalUboBuffer; 
    VkDescriptorSetLayout globalSetLayout; // 方便 Pass 随时取用 Layout
    
    VkDescriptorImageInfo albedoInfo;
    VkDescriptorImageInfo normalInfo;
    VkDescriptorImageInfo metallicInfo;
    VkDescriptorImageInfo roughnessInfo;
    VkDescriptorImageInfo environmentInfo;

    VkDescriptorImageInfo posInputInfo;
    VkDescriptorImageInfo normalInputInfo;
    VkDescriptorImageInfo albedoInputInfo;
    VkDescriptorImageInfo pbrInputInfo;
    
    VkDescriptorImageInfo ssaoInfo;
    
    VkDescriptorImageInfo dummyInfo; // 防报错替身图
};