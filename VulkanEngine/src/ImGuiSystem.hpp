#pragma once
#include "VulkanDevice.hpp"
#include "VulkanGameObject.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

class ImGuiSystem {
public:
    // 初始化需要窗口、设备、渲染通道和命令池（用于上传字体）
    ImGuiSystem(GLFWwindow* window, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);
    ~ImGuiSystem();

    // 禁止拷贝
    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;

    // 每一帧调用，生成 UI 并录制进 CommandBuffer
    void render(VkCommandBuffer commandBuffer, VulkanGameObject& cameraObj, float dt);

private:
    VulkanDevice& vulkanDevice;
    VkDescriptorPool imguiPool;
};