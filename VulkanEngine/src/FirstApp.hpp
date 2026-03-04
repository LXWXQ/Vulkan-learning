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
    FrameBufferAttachment pbr; // R: Metallic, G: Roughness, B: AO, A: 未使用
    FrameBufferAttachment depth;
    VkSampler sampler; // 光照阶段采样 G-Buffer 用
};

class FirstApp {
public:
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;

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

    // --- 核心模块引用 (智能指针管理生命周期) ---
    GLFWwindow* window;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<VulkanSwapchain> vulkanSwapchain;
    //std::unique_ptr<VulkanPipeline> vulkanPipeline;

    // --- App 层独有的调度资源 ---
    VkRenderPass renderPass;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    void loadGameObjects(); // 添加一个加载模型的函数

    VkDescriptorSetLayout globalSetLayout;
    void createDescriptorSetLayout();

    VkBuffer globalUboBuffer;
    VkDeviceMemory globalUboBufferMemory;
    void* uboMapped; // 永久映射的内存指针，极其高效，允许 C++ 每帧直接向显存写数据

    VkDescriptorPool descriptorPool;
    VkDescriptorSet globalDescriptorSet; // 交给 GPU 的那把钥匙
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();

    struct TextureData {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
    };

    // 4 张 PBR 核心贴图
    TextureData albedoTex;
    TextureData normalTex;
    TextureData metallicTex;
    TextureData roughnessTex;

    // 升级后的流水线函数
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
};