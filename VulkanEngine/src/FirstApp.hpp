#pragma once

#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanModel.hpp"
#include <memory>
#include <vector>
#include <chrono>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GlobalUbo {
    glm::mat4 projectionView{1.f}; 
    glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .1f}; 
    glm::vec3 lightDirection = glm::normalize(glm::vec3(1.f, -3.f, -1.f)); 
    alignas(16) glm::vec4 lightColor{1.f}; 
    alignas(16) glm::vec4 cameraPos; // 【新增】摄像机的绝对位置 (必须 16 字节对齐)
};

// 我们的遥控器数据包 (最大通常只有 128 字节，极其珍贵)
struct SimplePushConstantData {
    glm::mat4 modelMatrix{1.f}; 
    glm::mat4 normalMatrix{1.f}; // 专门给法线用的变换矩阵，防止缩放导致光照错误
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
    void createPipelineLayout();
    void createPipeline();
    void createCommandPool();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex);
    void createSyncObjects();
    void drawFrame();

    // --- 核心模块引用 (智能指针管理生命周期) ---
    GLFWwindow* window;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<VulkanSwapchain> vulkanSwapchain;
    std::unique_ptr<VulkanPipeline> vulkanPipeline;

    // --- App 层独有的调度资源 ---
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    std::unique_ptr<VulkanModel> vulkanModel;
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
};