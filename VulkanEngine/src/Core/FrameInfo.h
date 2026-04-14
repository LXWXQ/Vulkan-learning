#pragma once
#include <vulkan/vulkan.h>
#include "Scene/Model.h"
#include "Scene/GameObject.h"
#define MAX_POINT_LIGHTS 10
#define WIDTH 1280
#define HEIGHT 720


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

struct FrameInfo 
{
    uint32_t frameIndex;
    float frameTime;              
    VkCommandBuffer commandBuffer;
    VkDescriptorSet globalDescriptorSet;
    
    std::vector<GameObject>& gameObjects;

    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
};