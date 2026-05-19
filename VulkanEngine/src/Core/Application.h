#pragma once
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Renderer.h"
#include "RHI/GBuffer.h"
#include "RHI/Texture.h"
#include "Core/DescriptorManager.h"
#include "Core/ImGuiSystem.h"
#include "Render/RenderPipeline.h"
#include "Scene/Scene.h"
#include <memory>
#include <chrono>

class FirstApp
{
public:
	FirstApp();
	~FirstApp();

	FirstApp(const FirstApp&) = delete;
	FirstApp& operator=(const FirstApp&) = delete;

	void run();

private:
	void initWindow();
	void loadAllPBRTextures();
	void createUniformBuffers();
	void initMaterials();

	void update(float dt);
	void render(float dt);

	GLFWwindow* window = nullptr;
	std::unique_ptr<Device> vulkanDevice;
	std::unique_ptr<Swapchain> vulkanSwapchain;
	std::unique_ptr<Renderer> renderer;
	std::unique_ptr<GBuffer> gBuffer;
	std::unique_ptr<DescriptorManager> descriptorManager;
	std::unique_ptr<Scene> scene;
	std::unique_ptr<ImGuiSystem> imguiSystem;

	RenderPipeline renderPipeline;

	VkBuffer globalUboBuffer = VK_NULL_HANDLE;
	VmaAllocation globalUboAllocation = VK_NULL_HANDLE;
	void* uboMapped = nullptr;

	std::unique_ptr<Texture> environmentTex;

	std::chrono::high_resolution_clock::time_point currentTime;
};
