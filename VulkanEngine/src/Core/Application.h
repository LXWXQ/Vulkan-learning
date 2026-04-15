#pragma once

#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "Scene/Model.h"
#include "Render/GeometrySystem.h"
#include "Render/LightingSystem.h"
#include "Scene/Camera.h"
#include "Scene/CameraController.h"
#include "ImGuiSystem.h"
#include "RHI/Texture.h"
#include "RHI/GBuffer.h"
#include "RHI/Renderer.h"
#include "Render/RenderPipeline.h"
#include "Render/GeometryPass.h"
#include "Render/LightingPass.h"
#include "Render/SSAOPass.h"
#include "Core/Descriptor.h"
#include <memory>
#include <vector>
#include <chrono>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>





class FirstApp 
{
public:
   

    FirstApp();
    ~FirstApp();

    FirstApp(const FirstApp &) = delete;
    FirstApp &operator=(const FirstApp &) = delete;

    void run();

private:
    void initWindow();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createRenderTargets();
    void loadAllPBRTextures();
    void createDescriptorSetLayout();
    void loadGameObjects();
    void update(float dt);
    void render(float dt);
private:
    
    GLFWwindow* window = nullptr;
    std::unique_ptr<Device> vulkanDevice;
    std::unique_ptr<Swapchain> vulkanSwapchain;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<GBuffer> gBuffer;
    

    VkDescriptorSetLayout globalSetLayout;
    VkBuffer globalUboBuffer;
    VkDeviceMemory globalUboBufferMemory;
    void* uboMapped;
    VkDescriptorPool descriptorPool;
    //VkDescriptorSet globalDescriptorSet;
    std::vector<VkDescriptorSet> frameDescriptorSets;
    std::vector<std::unique_ptr<DescriptorAllocator>> frameDescriptorAllocators;

    std::unique_ptr<Texture> albedoTex;
    std::unique_ptr<Texture> normalTex;
    std::unique_ptr<Texture> metallicTex;
    std::unique_ptr<Texture> roughnessTex;
    std::unique_ptr<Texture> environmentTex;
    std::vector<GameObject> gameObjects;

    RenderPipeline renderPipeline;
    std::unique_ptr<ImGuiSystem> imguiSystem;
    GameObject cameraObject = GameObject::createGameObject();
    std::shared_ptr<Model> quadSphereModel = nullptr;

    VulkanCamera camera{};
    CameraController cameraController{};
    std::chrono::high_resolution_clock::time_point currentTime;
    std::unordered_map<std::string, VkDescriptorImageInfo> renderTargets;

    EngineSettings engineSettings;
    RenderTelemetry currentTelemetry;
};