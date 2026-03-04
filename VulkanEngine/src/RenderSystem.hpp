#pragma once

#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanModel.hpp"
#include "FrameInfo.hpp"
// RenderSystem.hpp (构想图)
class RenderSystem {
public:
    virtual ~RenderSystem() = default;

    // 初始化时，系统需要知道自己隶属于哪个 RenderPass 和 Subpass
    virtual void init(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout) = 0;
    
    // 渲染时，系统只接收一条总线数据
    virtual void render(FrameInfo& frameInfo) = 0;
};