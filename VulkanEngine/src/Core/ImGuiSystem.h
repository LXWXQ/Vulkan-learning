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
	void newFrame();
	ImGuiSystem(const ImGuiSystem&) = delete;
	ImGuiSystem& operator=(const ImGuiSystem&) = delete;
	void render(VkCommandBuffer commandBuffer, FrameInfo& frameInfo);

private:
	void renderPerformancePanel(float dt, RenderTelemetry& telemetry);
	void renderSSAOPanel(EngineSettings& settings);
	void renderLightingPanel(EngineSettings& settings);
	void renderCameraPanel(GameObject& cameraObj, class CameraController* controller);

	Device& vulkanDevice;
	VkDescriptorPool imguiPool;
};
