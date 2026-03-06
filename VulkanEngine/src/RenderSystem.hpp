#pragma once

#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanModel.hpp"
#include "FrameInfo.hpp"

class RenderSystem 
{
public:
    virtual ~RenderSystem() = default;
    virtual void init(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout) = 0;
    virtual void render(FrameInfo& frameInfo) = 0;
};