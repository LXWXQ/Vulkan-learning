#pragma once
#include "RHI/Device.h"
#include "Scene/GameObject.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "Core/FrameInfo.h"

class ImGuiSystem 
{
public:
    ImGuiSystem(GLFWwindow* window, Device& device, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);
    ~ImGuiSystem();
    void newFrame() ;
    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;
    void render(VkCommandBuffer commandBuffer, GameObject& cameraObj, float dt, EngineSettings& settings, RenderTelemetry& telemetry);
private:
    Device& vulkanDevice;
    VkDescriptorPool imguiPool;
};