#pragma once

#include "Device.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

class Swapchain 
{
    public:
        Swapchain(Device& deviceRef, VkExtent2D windowExtent);
        ~Swapchain();

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;

        VkSwapchainKHR getSwapchain() { return swapChain; }
        VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }
        VkExtent2D getSwapChainExtent() { return swapChainExtent; }
        uint32_t imageCount() { return swapChainImages.size(); }
        VkImageView getImageView(int index) { return swapChainImageViews[index]; }
        VkFramebuffer getFramebuffer(int index) { return swapChainFramebuffers[index]; }

        void createFramebuffers(VkRenderPass renderPass);
        VkImageView getDepthImageView() { return depthImageView; }

    private:
        void createSwapChain();
        void createImageViews();
        void createDepthResources();
        
        Device& device;
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