#pragma once
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Renderer.h"
#include "RHI/GBuffer.h"
#include "RHI/Texture.h"
#include "Core/DescriptorManager.h"
#include "Core/ImGuiSystem.h"
#include "Core/EngineConfig.h"
#include "Core/JobSystem.h"
#include "Render/RenderPipeline.h"
#include "Render/IBLBaker.h"
#include "Render/RenderResources.h"
#include "RHI/RenderGraph.h"
#include "Scene/Scene.h"
#include <memory>
#include <chrono>
#include <array>

class FirstApp
{
public:
	FirstApp();
	~FirstApp();

	FirstApp(const FirstApp&) = delete;
	FirstApp& operator=(const FirstApp&) = delete;

	void run();

	// ---- 无人值守自检（D-2 新增）----
	// 为什么不只在 UI 上勾选：ImGui 的复选框没法在无人值守的验证流程里点。
	// 但"关掉某个 Pass"恰恰是最容易出验证层错误的分支 —— 中间目标整帧
	// 没人写（布局一直停在 UNDEFINED），后面的 Pass 却仍然声明要采样它。
	// 所以把开关和固定帧数的跑法暴露出来，由命令行参数驱动，
	// 这样这些分支也能进"零验证层输出"的回归检查。
	void setBloomEnabled(bool enabled);
	void setSSAOEnabled(bool enabled);
	int  runHeadlessSelfTest(int frames);

	// W1：配置存取。ImGui 面板上的 Save/Load 按钮走这里 ——
	// 让"面板改的参数"和"启动时读的参数"共用同一条代码路径，
	// 避免出现"UI 保存的格式和启动读的格式不一致"这种低级但难查的问题。
	void saveConfig();
	void loadConfig();

private:
	void initWindow(const std::string& title);
	void loadAllPBRTextures();
	void createUniformBuffers();
	void initMaterials();

	void update(float dt);
	void render(float dt);

	GLFWwindow* window = nullptr;
	std::unique_ptr<Device> vulkanDevice;
	std::unique_ptr<Swapchain> vulkanSwapchain;
	std::unique_ptr<Renderer> renderer;
	std::array<std::unique_ptr<GBuffer>, kFramesInFlight> gBuffers;
	std::unique_ptr<DescriptorManager> descriptorManager;
	std::unique_ptr<Scene> scene;
	std::unique_ptr<JobSystem> jobSystem;
	std::unique_ptr<ImGuiSystem> imguiSystem;
	std::unique_ptr<IBLBaker> iblBaker;

	RenderPipeline renderPipeline;

	// D-1(b)：渲染图 + 帧内资源登记表
	std::unique_ptr<RenderGraph> renderGraph;
	std::unique_ptr<RenderResources> renderResources;

	std::array<VkBuffer, kFramesInFlight> globalUboBuffers{};
	std::array<VmaAllocation, kFramesInFlight> globalUboAllocations{};
	std::array<void*, kFramesInFlight> uboMapped{};

	std::unique_ptr<Texture> environmentTex;

	std::chrono::high_resolution_clock::time_point currentTime;
	std::chrono::high_resolution_clock::time_point lastPerfLog;
	uint32_t framesSincePerfLog = 0;
	double updateMsAccum = 0.0;
	double renderMsAccum = 0.0;
	uint32_t profiledFrames = 0;

	// 自检会故意把 bloom/ssao 关掉，如果照常自动保存，一次回归跑下来
	// 就把用户调好的配置改成了"全关"。所以自检模式下禁掉退出自动保存。
	bool suppressAutoSave = false;
};
