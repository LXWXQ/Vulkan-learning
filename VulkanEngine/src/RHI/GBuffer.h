#pragma once

#include "Device.h"
#include <vulkan/vulkan.h>
#include <stdexcept>

// 将原本的附件结构体移到这里
struct FrameBufferAttachment 
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format;
};

class GBuffer 
{
public:
    // 构造时需要 Device 和当前的屏幕分辨率(Extent)
    GBuffer(Device& device, VkExtent2D extent);
    
    // RAII 核心：自动销毁所有 GBuffer 附件
    ~GBuffer();

    // 禁用拷贝
    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    // Getters，供 Framebuffer 和 DescriptorSet 使用
    const FrameBufferAttachment& getPosition() const { return position; }
    const FrameBufferAttachment& getNormal() const { return normal; }
    const FrameBufferAttachment& getAlbedo() const { return albedo; }
    const FrameBufferAttachment& getPbr() const { return pbr; }
    const FrameBufferAttachment& getDepth() const { return depth; }
    VkSampler getSampler() const { return sampler; }

private:
    void createAttachment(VkFormat format, VkImageUsageFlags usage, FrameBufferAttachment* attachment);
    void destroyAttachment(FrameBufferAttachment& attachment);

    Device& device;
    VkExtent2D currentExtent;

    FrameBufferAttachment position;
    FrameBufferAttachment normal;
    FrameBufferAttachment albedo;
    FrameBufferAttachment pbr;
    FrameBufferAttachment depth;
    VkSampler sampler = VK_NULL_HANDLE;
};