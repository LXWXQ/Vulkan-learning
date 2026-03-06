#pragma once

#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanModel.hpp"
#include "GeometrySystem.hpp"
#include "LightingSystem.hpp"
#include "VulkanCamera.hpp"
#include "CameraController.hpp"
#include "ImGuiSystem.hpp"

#include <memory>
#include <vector>
#include <chrono>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct FrameBufferAttachment 
{
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
};

struct GBuffer 
{
    int32_t width, height;
    FrameBufferAttachment position;
    FrameBufferAttachment normal;
    FrameBufferAttachment albedo;
    FrameBufferAttachment pbr;
    FrameBufferAttachment depth;
    VkSampler sampler;
};

class FirstApp 
{
public:
    static constexpr int WIDTH = 1920;
    static constexpr int HEIGHT = 1080;

    FirstApp();
    ~FirstApp();

    FirstApp(const FirstApp &) = delete;
    FirstApp &operator=(const FirstApp &) = delete;

    void run();

private:
    void initWindow();
    void createRenderPass();
    void createCommandPool();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex, VulkanCamera& camera, float dt);
    void createSyncObjects();
    void drawFrame(VulkanCamera& camera, float dt);

    GLFWwindow* window;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<VulkanSwapchain> vulkanSwapchain;
    VkRenderPass renderPass;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    void loadGameObjects();

    VkDescriptorSetLayout globalSetLayout;
    void createDescriptorSetLayout();

    VkBuffer globalUboBuffer;
    VkDeviceMemory globalUboBufferMemory;
    void* uboMapped;

    VkDescriptorPool descriptorPool;
    VkDescriptorSet globalDescriptorSet;
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();

    struct TextureData 
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
    };

    TextureData albedoTex;
    TextureData normalTex;
    TextureData metallicTex;
    TextureData roughnessTex;

    void loadAllPBRTextures();
    void createSingleTexture(const std::string& filepath, TextureData& outTexture, VkFormat format);
    TextureData environmentTex;
    void createHDRTexture(const std::string& filepath, TextureData& outTexture);

    GBuffer gBuffer;
    void createGBufferResources();
    void createAttachment(VkFormat format, VkImageUsageFlags usage, FrameBufferAttachment* attachment);
    std::vector<VkFramebuffer> framebuffers;
    void createFramebuffers();
    std::unique_ptr<GeometrySystem> geometrySystem;
    std::unique_ptr<LightingSystem> lightingSystem;
    std::vector<VulkanGameObject> gameObjects;

    std::unique_ptr<ImGuiSystem> imguiSystem;
    VulkanGameObject cameraObject = VulkanGameObject::createGameObject();
    std::shared_ptr<VulkanModel> quadSphereModel = nullptr;
};