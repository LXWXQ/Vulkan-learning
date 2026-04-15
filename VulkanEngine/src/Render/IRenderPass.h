#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "Core/FrameInfo.h"

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    // 初始化资源（RenderPass, Framebuffer, Pipeline 等）
    virtual void init() = 0;
    
    // 执行渲染逻辑
    virtual void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) = 0;

    // 处理窗口缩放
    virtual void onResize(VkExtent2D extent) = 0;

    virtual const std::string getName() const = 0; // 每个 Pass 都要有个名字，方便调试和 ImGui 显示
};