#pragma once
#include "IRenderPass.h"
#include "RHI/Device.h"
#include "RHI/GBuffer.h"
#include "GeometrySystem.h"
#include <memory>

class GeometryPass : public IRenderPass 
{
public:
    // 构造函数改为接收 setLayout，去掉 System
    GeometryPass(Device& device, GBuffer& gBuffer, VkDescriptorSetLayout setLayout);
    ~GeometryPass() override;

    void init() override;
    void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) override;
    void onResize(VkExtent2D extent) override {}

    VkRenderPass getRenderPass() const { return renderPass; }
    const std::string getName() const override { return "Geometry Pass"; }
private:
    void createRenderPass();
    void createFramebuffer();

    Device& vulkanDevice;
    GBuffer& gBuffer;
    VkDescriptorSetLayout globalSetLayout; // 存下来备用

    // 自己独占并管理 GeometrySystem
    std::unique_ptr<GeometrySystem> geometrySystem;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};