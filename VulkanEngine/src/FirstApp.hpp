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

// 我们的遥控器数据包 (最大通常只有 128 字节，极其珍贵)
struct SimplePushConstantData {
    glm::mat2 transform{1.f}; // 2x2 变换矩阵
    glm::vec2 offset;         // 2D 平移
    alignas(16) glm::vec3 color; // 强制 16 字节对齐防坑，用于改变颜色
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
};