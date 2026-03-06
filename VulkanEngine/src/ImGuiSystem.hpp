#pragma once
#include "VulkanDevice.hpp"
#include "VulkanGameObject.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

class ImGuiSystem 
{
public:
    ImGuiSystem(GLFWwindow* window, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);
    ~ImGuiSystem();
    void newFrame() ;
    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;
    void render(VkCommandBuffer commandBuffer, VulkanGameObject& cameraObj, float dt);

private:
    VulkanDevice& vulkanDevice;
    VkDescriptorPool imguiPool;
};