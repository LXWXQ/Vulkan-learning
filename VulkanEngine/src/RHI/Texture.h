#pragma once

#include "Device.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

class Texture 
{
public:
    // 构造时不仅需要 Device，还需要 CommandPool 来执行图片内存屏障和拷贝
    Texture(Device& device, const std::string& filepath, VkFormat format, VkCommandPool commandPool, bool isHDR = false);
    
    // RAII 核心：析构函数自动清理资源
    ~Texture();

    // 禁用拷贝构造和赋值操作，防止资源被意外双重释放
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // 允许移动语义（如果需要放进 std::vector）
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Getters
    VkImageView getView() const { return view; }
    VkSampler getSampler() const { return sampler; }
    VkImage getImage() const { return image; }

private:
    void loadLDR(const std::string& filepath, VkFormat format, VkCommandPool commandPool);
    void loadHDR(const std::string& filepath, VkCommandPool commandPool);

    Device& vulkanDevice;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};