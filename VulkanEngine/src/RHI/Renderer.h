#pragma once

#include "Device.h"
#include "Swapchain.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

class Renderer 
{
public:
    Renderer(Device& device, Swapchain& swapchain);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // 核心接口：控制帧的生命周期
    VkCommandBuffer beginFrame();
    void endFrame();
    
    // D-1(b) 起渲染工作全部交给 RenderGraph（动态渲染），
    // Renderer 只负责帧生命周期（获取图像 / 提交 / present）。

    // Getters
    uint32_t getCurrentImageIndex() const { return currentImageIndex; }
    VkCommandPool getCommandPool() const { return commandPool; }

private:
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    Device& vulkanDevice;
    Swapchain& vulkanSwapchain;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // 帧同步对象
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    uint32_t currentImageIndex = 0;
};