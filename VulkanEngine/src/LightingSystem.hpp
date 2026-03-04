#pragma once
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "FrameInfo.hpp"
#include <memory>

class LightingSystem {
public:
    LightingSystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~LightingSystem();

    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;

    void render(FrameInfo& frameInfo);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipeline(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    VkPipelineLayout pipelineLayout;
    std::unique_ptr<VulkanPipeline> lightingPipeline;
};