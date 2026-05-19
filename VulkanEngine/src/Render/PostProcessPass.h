#pragma once
#include "IRenderPass.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include <memory>
#include <vector>

class PostProcessPass : public IRenderPass 
{
public:
    PostProcessPass(Device& device, VkExtent2D extent, VkFormat outputFormat);
    virtual ~PostProcessPass() override;

    // 基类通用的初始化，子类可以在里面重写并调用基类
    virtual void init() override;
    
    // 子类必须实现具体的执行逻辑（绑定管线、描述符等）
    virtual void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) override = 0;
    
    virtual void onResize(VkExtent2D extent) override;

    // 暴露输出结果，供下一个 Pass (如 LightingPass) 读取
    VkImageView getOutputImageView() const { return outputImage.view; }
    VkSampler getOutputSampler() const { return outputSampler; }

protected:
    Device& device;
    VkExtent2D currentExtent;
    VkFormat format;

    // 后处理专属的 RenderPass 和 Framebuffer
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    // 输出的贴图资源 (子类算完的结果存这里)
    struct RenderTarget {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } outputImage;
    VkSampler outputSampler = VK_NULL_HANDLE;

    // 内部助手函数
    void createRenderTarget();
    void createRenderPass();
    void createFramebuffer();
    void destroyRenderTarget();
};