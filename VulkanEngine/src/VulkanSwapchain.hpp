#pragma once

#include "VulkanDevice.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

class VulkanSwapchain 
{
public:
    // 构造函数：强依赖一个 VulkanDevice 实例，并传入窗口分辨率
    VulkanSwapchain(VulkanDevice& deviceRef, VkExtent2D windowExtent);
    ~VulkanSwapchain();

    // 禁用拷贝，Vulkan 资源必须独占
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // --- 对外接口 (Getters) ---
    VkSwapchainKHR getSwapchain() { return swapChain; }
    VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() { return swapChainExtent; }
    uint32_t imageCount() { return swapChainImages.size(); }
    VkImageView getImageView(int index) { return swapChainImageViews[index]; }
    VkFramebuffer getFramebuffer(int index) { return swapChainFramebuffers[index]; }

    // 哨兵提醒：相框的创建必须延后，因为它需要外部传入一个“工序单”(RenderPass)
    void createFramebuffers(VkRenderPass renderPass);
    VkImageView getDepthImageView() { return depthImageView; }
private:
    void createSwapChain();
    void createImageViews();
    void createDepthResources();
    // --- 核心成员变量 ---
    VulkanDevice& device; // 这是一个引用！代表它不拥有设备，只是使用设备
    VkExtent2D windowExtent;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;

    VkFormat depthFormat;
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
};