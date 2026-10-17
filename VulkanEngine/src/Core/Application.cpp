#include "Application.h"
#include "AssetPaths.h"
#include "Core/FrameInfo.h"
#include "Render/GeometryPass.h"
#include "Render/LightingPass.h"
#include "Render/BloomPass.h"
#include "Render/PresentPass.h"
#include "Render/SSAOPass.h"
#include "Render/ComputeProbePass.h"
#include "Render/CrossQueueProbePass.h"
#include "Render/SubresourceProbePass.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include "Core/Profiler.h"
#include "RHI/GpuProfiler.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

FirstApp::FirstApp(const diag::Options& options)
{
	diagnosticsOptions = options;

	// W1：配置必须在最前面读 —— 窗口标题就是配置里的一项，而窗口在
	// initWindow() 里创建，之后再改标题会让用户看到一次闪动。
	// 这里先读进一个局部对象，等 Scene 就绪后再整体搬过去。
	// 读失败（文件不存在/语法错）不是错误：EngineSettings 的默认值就是
	// 设计好的出厂预设，EngineConfig 会在控制台说明原因。
	EngineSettings loadedSettings;
	EngineConfig::load(loadedSettings);

	CameraState loadedCamera;                 // 同样的道理：相机位姿也要在
	const bool hasCamera = EngineConfig::load(loadedCamera);   // Scene 之后才能应用

	// D-6：先启动 CPU worker 池。它不依赖 Vulkan，后续场景加载/地形生成可以复用。
	jobSystem = std::make_unique<JobSystem>();

	initWindow(loadedSettings.windowTitle);
	vulkanDevice = std::make_unique<Device>(window, diagnosticsOptions);

	// 请求尺寸只是期望值。surface 可能因为标题栏/任务栏把它裁小，
	// 交换链创建时会采用 surface 的 currentExtent；渲染资源必须跟着实际尺寸走。
	VkExtent2D requestedExtent{ WIDTH, HEIGHT };
	vulkanSwapchain = std::make_unique<Swapchain>(*vulkanDevice, requestedExtent);
	const VkExtent2D renderExtent = vulkanSwapchain->getSwapChainExtent();
	currentRenderExtent = renderExtent;
	renderer = std::make_unique<Renderer>(*vulkanDevice, *vulkanSwapchain);

	// P1-4：无头截图（编译期或运行期任一关闭时 init 直接返回，不创建任何资源）。
	screenshotCapture.init(vulkanDevice->getDevice(), vulkanDevice->getVmaAllocator(),
		vulkanSwapchain->getSwapChainImageFormat(), vulkanSwapchain->getSwapChainExtent(),
		diagnosticsOptions.screenshot);
	if (screenshotCapture.isEnabled() && diagnosticsOptions.screenshotFrame >= 0)
		screenshotCapture.setTargetFrame(static_cast<uint64_t>(diagnosticsOptions.screenshotFrame));
	else
		screenshotAutoTarget = screenshotCapture.isEnabled();

	// P1-2：RenderDoc 抓帧挂钩（编译期关闭或未注入时为空操作）。
	renderDoc.init(diagnosticsOptions.captureFrame > 0, diagnosticsOptions.captureFrame);

	// D-4：每个 frame slot 一套 G-Buffer。两帧同时在飞时共享一套会跨帧读写冲突。
	std::array<GBuffer*, kFramesInFlight> gBufferPtrs{};
	for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
	{
		gBuffers[frame] = std::make_unique<GBuffer>(*vulkanDevice, renderExtent);
		gBufferPtrs[frame] = gBuffers[frame].get();
	}

	// D-1(b)：渲染图必须在各 Pass 之前创建 —— Pass 的 init() 会往里注册资源
	renderGraph = std::make_unique<RenderGraph>(*vulkanDevice);

	// P0-4：GPU 计时。编译期或运行期任一关闭时 init 直接返回，不创建任何资源。
	gpuTimer.init(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice(),
		vulkanDevice->findPhysicalQueueFamilies().graphicsFamily.value(),
		kFramesInFlight, diagnosticsOptions.gpuTiming);
	renderGraph->setGpuTimer(&gpuTimer);

	// P1-1：Tracy GPU 上下文（每 pass zone）。编译期关闭时不创建任何资源。
	gpuProfiler.init(vulkanDevice->getPhysicalDevice(), vulkanDevice->getDevice(),
		vulkanDevice->getGraphicsQueue(), renderer->getCommandPool(), true);
	renderGraph->setGpuProfiler(&gpuProfiler);
	if (diagnosticsOptions.gpuTiming)
	{
		if (gpuTimer.isEnabled())
			std::cout << "[Diag] GPU timing enabled -> " << diagnosticsOptions.gpuTimingCsvPath
			          << " (queries=" << gpuTimer.queryPoolCount() << ")\n";
		else
			std::cout << "[Diag] --gpu-timing not active (not compiled in or device lacks timestamps)\n";
	}
	renderResources = std::make_unique<RenderResources>();

	// 注意顺序：Scene::init 里要加载 glTF，而 glTF 材质需要立刻分配描述符槽位，
	// 所以 DescriptorManager 必须先于 Scene 创建并初始化。
	descriptorManager = std::make_unique<DescriptorManager>();
	descriptorManager->init(*vulkanDevice, 256);
	descriptorManager->setFrameTimelineCallbacks(
		[this] { return renderer->getCompletedGraphicsFrame(); },
		[this](uint64_t frame) { renderer->waitForGraphicsFrame(frame); },
		[this] { return renderer->getLastSubmittedGraphicsFrame(); });

	scene = std::make_unique<Scene>();
	scene->init(*vulkanDevice, renderer->getCommandPool(), *descriptorManager);
	scene->getCameraController().registerCallbacks(window);

	// W1：把配置应用到场景。顺序很重要 —— Scene::init 里会摆好默认相机位姿，
	// 配置只能在那之后再覆盖，否则会被 init 的默认值抹掉。
	scene->getSettings() = loadedSettings;
	if (hasCamera)
	{
		scene->getCameraTransform().translation = loadedCamera.position;
		scene->getCameraTransform().rotation = loadedCamera.rotation;
		std::cout << "[Config] 已应用相机位姿 ("
			<< loadedCamera.position.x << ", " << loadedCamera.position.y << ", " << loadedCamera.position.z << ")\n";
	}

	loadAllPBRTextures();

	// 烘焙 IBL：必须在 buildGlobalSet 之前完成，因为需要把结果写进描述符集。
	// 这一步是纯 GPU 计算（irradiance 卷积 / GGX prefilter / BRDF 积分表），
	// 放在这里是因为此时环境贴图已经上传完毕。
	iblBaker = std::make_unique<IBLBaker>(*vulkanDevice, renderer->getCommandPool());
	iblBaker->bake(*environmentTex);
	createUniformBuffers();
	initMaterials();

	auto geoPass = std::make_unique<GeometryPass>(*vulkanDevice, gBufferPtrs, *renderGraph, *renderResources,
		descriptorManager->getGlobalSetLayout(), descriptorManager->getMaterialSetLayout());
	auto ssaoPass = std::make_unique<SSAOPass>(*vulkanDevice, *renderGraph, *renderResources,
		descriptorManager->getGlobalSetLayout(), descriptorManager.get());
	auto lightPass = std::make_unique<LightingPass>(*vulkanDevice, *gBuffers[0], *vulkanSwapchain,
		*renderGraph, *renderResources, descriptorManager->getGlobalSetLayout());
	// D-2 的两个新成员：Bloom 只负责在 HDR 图上生成辉光，
	// PresentPass 负责把 HDR + 辉光变成最终显示值并叠加 ImGui。
	auto bloomPass = std::make_unique<BloomPass>(*vulkanDevice, *renderGraph, *renderResources,
		descriptorManager.get());
	auto presentPass = std::make_unique<PresentPass>(*vulkanDevice, *vulkanSwapchain, *renderGraph,
		*renderResources, descriptorManager.get());

	auto computeProbePass = std::make_unique<ComputeProbePass>(*vulkanDevice, *renderGraph,
		descriptorManager.get());
	auto crossQueueProbePass = std::make_unique<CrossQueueProbePass>(*vulkanDevice, *renderGraph,
		descriptorManager.get());
	auto subresourceProbePass = std::make_unique<SubresourceProbePass>(*vulkanDevice, *renderGraph);

	presentPassPtr = presentPass.get();
	ssaoPassPtr = ssaoPass.get();
	computeProbePassPtr = computeProbePass.get();
	crossQueueProbePassPtr = crossQueueProbePass.get();
	subresourceProbePassPtr = subresourceProbePass.get();

	renderPipeline.addPass(std::move(geoPass));
	renderPipeline.addPass(std::move(ssaoPass));
	renderPipeline.addPass(std::move(lightPass));
	renderPipeline.addPass(std::move(bloomPass));
	renderPipeline.addPass(std::move(presentPass));
	renderPipeline.addPass(std::move(crossQueueProbePass));
	renderPipeline.addPass(std::move(subresourceProbePass));
	renderPipeline.addPass(std::move(computeProbePass));

	renderPipeline.initAll();

	// ---- 把跨帧常驻的只读资源导入渲染图 ----
	// initialLayout 给 SHADER_READ_ONLY，渲染图会把它们标成「跨帧常驻」，
	// 每帧不复位布局记录（否则会被当成「内容未定义」而丢弃）。
	auto importPersistent = [&](const char* name, VkImage img, VkImageView view,
		VkFormat fmt, uint32_t layers, VkImageCreateFlags flags)
	{
		RenderGraph::TextureDesc d;
		d.name = name;
		d.format = fmt;
		d.extent = vulkanSwapchain->getSwapChainExtent();
		d.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		d.arrayLayers = layers;
		d.flags = flags;
		return renderGraph->importTexture(d, img, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

	const VkImageCreateFlags cubeFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	renderResources->environment = importPersistent("Environment",
		environmentTex->getImage(), environmentTex->getView(), VK_FORMAT_R32G32B32A32_SFLOAT, 1, 0);
	renderResources->irradiance = importPersistent("IBL.Irradiance",
		iblBaker->getIrradianceMap().getImage(), iblBaker->getIrradianceMap().getView(),
		VK_FORMAT_R16G16B16A16_SFLOAT, 6, cubeFlags);
	renderResources->prefiltered = importPersistent("IBL.Prefiltered",
		iblBaker->getPrefilteredMap().getImage(), iblBaker->getPrefilteredMap().getView(),
		VK_FORMAT_R16G16B16A16_SFLOAT, 6, cubeFlags);
	renderResources->brdfLut = importPersistent("IBL.BrdfLut",
		iblBaker->getBrdfLutImage(), iblBaker->getBrdfLutView(), VK_FORMAT_R16G16_SFLOAT, 1, 0);

	rebuildGlobalDescriptors();

	// D-1(b)：ImGui 改用动态渲染，只需要交换链颜色格式。
	// D-2   ：UI 的绘制不再属于光照通道 —— 它由 PresentPass 在最终图像上叠加，
	//         这样以后往 Present 之前插任何后效都不会把 UI 一起糊掉。
	imguiSystem = std::make_unique<ImGuiSystem>(window, *vulkanDevice,
		vulkanSwapchain->getSwapChainImageFormat(), vulkanSwapchain->imageCount(),
		renderer->getCommandPool());
	presentPassPtr->setImGuiSystem(imguiSystem.get());

	currentTime = std::chrono::high_resolution_clock::now();
	lastPerfLog = currentTime;
}

FirstApp::~FirstApp()
{
	// =====================================================================
	// 【为什么这里要把销毁顺序全部显式写出来】
	//
	// 原来的写法是：函数体里先 glfwDestroyWindow + glfwTerminate，其余成员
	// 等析构体结束后按逆序自动销毁。两个问题：
	//
	//   1) 窗口和 VkSurface 先没了，Device / Swapchain / ImGuiSystem 才开始
	//      销毁。能跑通只是因为当前驱动容忍度高。
	//
	//   2) 更隐蔽的一个：**renderPipeline 是值成员，而它在 Application.h 里
	//      声明得比 vulkanDevice 晚，所以成员逆序析构时它排在 vulkanDevice
	//      之后销毁** —— 也就是说 Pass 持有的 VMA 分配（SSAOPass 的 UBO 等）
	//      会在 vmaDestroyAllocator 之后才被释放，直接触发 VMA 的
	//      "Some allocations were not freed" 断言崩溃。
	//
	//      这个 bug 一直存在，只是以前没暴露：验证脚本是用
	//      TerminateProcess 杀进程的，而 TerminateProcess 根本不跑析构函数。
	//      直到 W1 需要"退出自动保存配置"、改成 WM_CLOSE 正常关窗，它才浮出来。
	//
	// 结论：不要依赖成员声明顺序来表达销毁依赖 —— 有人插一个新成员就会
	// 悄悄破坏它，而且编译器一声不吭。下面按"依赖方向"显式排好。
	// =====================================================================
	vkDeviceWaitIdle(vulkanDevice->getDevice());

	finalizeDiagnostics(); 
	// W1：退出时保存。放在析构里而不是 run() 的循环之后，是因为自检和异常
	// 退出也走析构，这样"改完参数直接关窗口"这条最常用的路径一定不会丢。
	// 但自检模式要跳过：它会把 bloom/ssao 关掉，照常保存就污染了用户配置。
	if (!suppressAutoSave)
		saveConfig();
	else
		std::cout << "[Config] 自检模式：跳过退出自动保存，避免污染用户配置\n";

	// 1) Pass 持有 GPU 资源（各自的 UBO / 采样器 / 管线），必须最先销毁。
	renderPipeline.clearAll();

	// 2) 依赖窗口的东西：ImGui 的 GLFW 后端持有窗口指针，
	//    Swapchain 的 image view 依赖 VkSurface（surface 又依赖窗口）。
	imguiSystem.reset();
	vulkanSwapchain.reset();

	// 3) 其余 GPU 资源
	for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
	{
		if (globalUboBuffers[frame] != VK_NULL_HANDLE)
			vmaDestroyBuffer(vulkanDevice->getVmaAllocator(),
				globalUboBuffers[frame], globalUboAllocations[frame]);
	}

	descriptorManager->cleanup();
	scene->cleanup();

	environmentTex.reset();
	iblBaker.reset();
	renderGraph.reset();          // 渲染图持有大量 image/view，必须在 Device 之前
	renderResources.reset();
	for (auto& gBuffer : gBuffers)
		gBuffer.reset();
	renderer.reset();
	descriptorManager.reset();
	scene.reset();

	// 4) Device 最后（它会销毁 VMA 分配器和 VkDevice）
	vulkanDevice.reset();

	// 5) 窗口系统收尾
	glfwDestroyWindow(window);
	glfwTerminate();
}

void FirstApp::initWindow(const std::string& title)
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	window = glfwCreateWindow(WIDTH, HEIGHT, title.c_str(), nullptr, nullptr);
}

void FirstApp::setResizeTestEnabled(bool enabled)
{
	resizeTestEnabled = enabled;
}

void FirstApp::setMaterialStressEnabled(bool enabled)
{
	materialStressEnabled = enabled;
}

void FirstApp::materialStressStep()
{
	if (materialStressPendingSlot != kInvalidMaterialSlot)
	descriptorManager->releaseMaterialSet(materialStressPendingSlot);

	auto material = scene->getMaterialSystem().getOrCreateMaterial("__TD04Stress");
	materialStressPendingSlot = descriptorManager->allocateMaterialSet(*material);
	++materialStressCycles;
	materialStressMaxPending = std::max(materialStressMaxPending,
		descriptorManager->getPendingMaterialReleaseCount());
}

void FirstApp::setComputeProbeEnabled(bool enabled)
{
	computeProbePassPtr->setEnabled(enabled);
}

void FirstApp::setSubresourceTestEnabled(bool enabled)
{
if (subresourceProbePassPtr != nullptr)
subresourceProbePassPtr->setMode(enabled
? SubresourceProbePass::Mode::Positive
: SubresourceProbePass::Mode::Disabled);
}

int FirstApp::runDeviceFaultSelfTest()
{
	suppressAutoSave = true;

	// P2-4：注意 VUID-vkGetDeviceFaultInfoEXT-device-07336 —— 该扩展只能在设备
	// "已 lost" 时调用，所以这里只校验"能力是否就绪"（扩展已启用 + 入口点已解析），
	// 真正的查询放在 Renderer 检测到 VK_ERROR_DEVICE_LOST 的那条路径上。
	const bool supported = vulkanDevice->isDeviceFaultSupported();
	const bool entryPoint = vulkanDevice->hasDeviceFaultEntryPoint();
	const bool ok = (supported == entryPoint);   // 要么都能用，要么都不支持

	std::cout << "[DeviceFault] selfTest " << (ok ? "PASS" : "FAIL")
	          << " | supported=" << (supported ? 1 : 0)
	          << " entryPoint=" << (entryPoint ? 1 : 0)
	          << " (query path reserved for lost-state)" << "\n";

	vkDeviceWaitIdle(vulkanDevice->getDevice());
	return ok ? 0 : 1;
}

int FirstApp::runSubresourceErrorSelfTest()
{
suppressAutoSave = true;
subresourceProbePassPtr->setMode(SubresourceProbePass::Mode::SemanticError);

try
{
glfwPollEvents();
if (!handleWindowMaintenance())
throw std::runtime_error("window is not renderable during TD-05 subresource test");
update(1.0f / 60.0f);
render(1.0f / 60.0f);
}
catch (const std::exception& e)
{
const std::string message = e.what();
if (message.find("mip 1 / layer 0") != std::string::npos &&
message.find("TD05.ReadMip1Layer0") != std::string::npos)
{
std::cout << "[TD05] subresource semantic error PASS: expected rejection observed\n";
vkDeviceWaitIdle(vulkanDevice->getDevice());
return 0;
}

std::cerr << "[TD05] unexpected error during negative test: " << message << "\n";
vkDeviceWaitIdle(vulkanDevice->getDevice());
return 1;
}

std::cerr << "[TD05] negative test failed: expected semantic error was not thrown\n";
vkDeviceWaitIdle(vulkanDevice->getDevice());
return 1;
}

void FirstApp::setRenderScale(float scale)
{
	scene->getSettings().renderScale = std::clamp(scale, 0.25f, 1.0f);
}

VkExtent2D FirstApp::getDesiredRenderExtent() const
{
	const VkExtent2D swapExtent = vulkanSwapchain->getSwapChainExtent();
	const float scale = std::clamp(scene->getSettings().renderScale, 0.25f, 1.0f);
	return VkExtent2D{
		std::max(1u, static_cast<uint32_t>(swapExtent.width * scale + 0.5f)),
		std::max(1u, static_cast<uint32_t>(swapExtent.height * scale + 0.5f))
	};
}

void FirstApp::rebuildGlobalDescriptors()
{
	VkDescriptorImageInfo envInfo{ environmentTex->getSampler(), environmentTex->getView(),
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	const CubemapTexture& irradiance = iblBaker->getIrradianceMap();
	const CubemapTexture& prefiltered = iblBaker->getPrefilteredMap();
	VkDescriptorImageInfo irradianceInfo{ irradiance.getSampler(), irradiance.getView(),
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo prefilteredInfo{ prefiltered.getSampler(), prefiltered.getView(),
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo brdfLutInfo{ iblBaker->getBrdfLutSampler(), iblBaker->getBrdfLutView(),
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	std::array<VkImageView, kFramesInFlight> ssaoViews{};
	std::array<const GBuffer*, kFramesInFlight> constGBufferPtrs{};
	for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
	{
		ssaoViews[frame] = ssaoPassPtr->getOutputImageView(frame);
		constGBufferPtrs[frame] = gBuffers[frame].get();
	}

	descriptorManager->buildGlobalSet(globalUboBuffers, envInfo, constGBufferPtrs,
	ssaoPassPtr->getOutputSampler(), ssaoViews,
	irradianceInfo, prefilteredInfo, brdfLutInfo);
}

bool FirstApp::handleWindowMaintenance()
{
	if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
	return false;

	int framebufferWidth = 0;
	int framebufferHeight = 0;
	glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
	if (framebufferWidth <= 0 || framebufferHeight <= 0)
	return false;

	const VkExtent2D framebufferExtent{
		static_cast<uint32_t>(framebufferWidth),
		static_cast<uint32_t>(framebufferHeight)
	};
	const VkExtent2D swapExtent = vulkanSwapchain->getSwapChainExtent();
	if (renderer->needsSwapchainRecreate() ||
	framebufferExtent.width != swapExtent.width ||
	framebufferExtent.height != swapExtent.height)
	{
		recreateSwapchain(framebufferExtent);
	}
	else
	{
		const VkExtent2D desiredExtent = getDesiredRenderExtent();
		if (desiredExtent.width != currentRenderExtent.width ||
		desiredExtent.height != currentRenderExtent.height)
		recreateRenderTargets(desiredExtent);
	}
	return true;
}

void FirstApp::recreateRenderTargets(VkExtent2D renderExtent)
{
	if (renderExtent.width == 0 || renderExtent.height == 0)
	return;

	vkDeviceWaitIdle(vulkanDevice->getDevice());

	for (auto& gBuffer : gBuffers)
	gBuffer->resize(renderExtent);

	renderPipeline.resizeAll(renderExtent);
	rebuildGlobalDescriptors();
	currentRenderExtent = renderExtent;

	std::cout << "[TD03] Render targets resized: "
	<< renderExtent.width << "x" << renderExtent.height
	<< " (swapchain " << vulkanSwapchain->getSwapChainExtent().width << "x"
	<< vulkanSwapchain->getSwapChainExtent().height << ")\n";
}

void FirstApp::recreateSwapchain(VkExtent2D framebufferExtent)
{
	if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
	return;

	vkDeviceWaitIdle(vulkanDevice->getDevice());

	vulkanSwapchain->recreate(framebufferExtent);
	renderer->onSwapchainRecreated();
	screenshotCapture.onResize(vulkanSwapchain->getSwapChainExtent());

	// ImGui's Vulkan backend caches image count and output format. Rebuild it
	// only with the swapchain; dynamic resolution keeps the same backend.
	imguiSystem.reset();
	imguiSystem = std::make_unique<ImGuiSystem>(window, *vulkanDevice,
	vulkanSwapchain->getSwapChainImageFormat(), vulkanSwapchain->imageCount(),
	renderer->getCommandPool());
	presentPassPtr->setImGuiSystem(imguiSystem.get());

	const VkExtent2D renderExtent = getDesiredRenderExtent();
	for (auto& gBuffer : gBuffers)
	gBuffer->resize(renderExtent);

	renderPipeline.resizeAll(renderExtent);
	rebuildGlobalDescriptors();
	currentRenderExtent = renderExtent;

	std::cout << "[TD03] Swapchain recreated to "
	<< vulkanSwapchain->getSwapChainExtent().width << "x"
	<< vulkanSwapchain->getSwapChainExtent().height
	<< ", render targets " << renderExtent.width << "x" << renderExtent.height << "\n";
}



void FirstApp::createUniformBuffers()
{
	// D-4：每帧槽位一份 UBO。单份 UBO 在 CPU/GPU 重叠时会被下一帧的 memcpy
	// 覆盖，GPU 可能读到新旧混合的数据。
	static const char* const names[kFramesInFlight] = { "GlobalUbo.Frame0", "GlobalUbo.Frame1" };

	for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
	{
		VkDeviceSize bufferSize = sizeof(GlobalUbo);
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = bufferSize;
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocInfo.flags |= sentinel_vmaNameFlag();
		allocInfo.pUserData = const_cast<char*>(names[frame]);

		if (vmaCreateBuffer(vulkanDevice->getVmaAllocator(), &bufferInfo, &allocInfo,
			&globalUboBuffers[frame], &globalUboAllocations[frame], nullptr) != VK_SUCCESS)
			throw std::runtime_error("GlobalUbo: per-frame buffer creation failed");

		VmaAllocationInfo info;
		vmaGetAllocationInfo(vulkanDevice->getVmaAllocator(), globalUboAllocations[frame], &info);
		uboMapped[frame] = info.pMappedData;
	}
}

void FirstApp::loadAllPBRTextures()
{
std::string basePath = SENTINEL_RESOURCE_DIR;

// HDR 环境贴图：天空盒 + IBL 都要用，无条件加载
environmentTex = std::make_unique<Texture>(*vulkanDevice,
basePath + "Texture/environment.hdr",
VK_FORMAT_R32G32B32A32_SFLOAT, renderer->getCommandPool(), true);

std::cout << "[Sentinel] HDR environment loaded.\n";
}

void FirstApp::initMaterials()
{
auto& matSys = scene->getMaterialSystem();
std::string basePath = SENTINEL_RESOURCE_DIR;

// ---- 兜底材质 ----
// 天空盒 / 网格虽然着色器里不读 set 1，但管线布局声明了 set 1，
// vkCmdBindDescriptorSets 不允许绑 VK_NULL_HANDLE。
// 原来靠"它们默认的 materialSetIndex 恰好是 0，而 0 号槽已经分配过"
// 才没崩 —— 这里显式给一个纯白兜底材质，把巧合变成保证。
auto defaultMat = matSys.getOrCreateMaterial("__EngineDefault");
matSys.buildMaterialDescriptor(defaultMat, *descriptorManager);

// ---- 判断是否真的需要 DamagedHelmet 材质 ----
// glTF 物体的材质已经由加载器逐子网格分配好了；
// 只有子网格没有材质的物体（OBJ 路径）才需要在这里统一指定。
auto& registry = scene->getRegistry();
auto needsHeroMaterial = [&]() -> bool
{
    for (entt::entity entity : registry.view<MeshComponent, RenderTagComponent>())
    {
        const auto& mesh = registry.get<MeshComponent>(entity);
        const auto& tag = registry.get<RenderTagComponent>(entity);
        if (!mesh.model || tag.isSkybox) continue;
        for (const auto& sub : mesh.model->getSubMeshes())
            if (sub.materialSlot == kInvalidMaterialSlot) return true;
    }
    return false;
}();

std::shared_ptr<Material> heroMat = defaultMat;
if (needsHeroMaterial)
{
// albedo 是颜色贴图 -> sRGB；normal / metalRoughness 是数据贴图 -> UNORM。
// 只在真的用得到时才加载，避免 Sponza 场景白白吃掉 48MB 显存。
matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_albedo.jpg",
*vulkanDevice, renderer->getCommandPool(), /*srgb=*/true);
matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_normal.jpg",
*vulkanDevice, renderer->getCommandPool(), /*srgb=*/false);
matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg",
*vulkanDevice, renderer->getCommandPool(), /*srgb=*/false);

heroMat = matSys.getOrCreateMaterial("DamagedHelmet");
heroMat->albedoMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_albedo.jpg");
heroMat->normalMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_normal.jpg");
heroMat->metallicRoughnessMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg");
matSys.buildMaterialDescriptor(heroMat, *descriptorManager);
}

// 只给"子网格没有材质"的物体指定整体材质；glTF 物体原样跳过
for (entt::entity entity : registry.view<MeshComponent, RenderTagComponent>())
{
    const auto& mesh = registry.get<MeshComponent>(entity);
    const auto& tag = registry.get<RenderTagComponent>(entity);
    if (!mesh.model) continue;

    bool hasPerSubMeshMaterial = true;
    for (const auto& sub : mesh.model->getSubMeshes())
        if (sub.materialSlot == kInvalidMaterialSlot) { hasPerSubMeshMaterial = false; break; }
    if (hasPerSubMeshMaterial) continue;

    MaterialComponent& materialComp = registry.get_or_emplace<MaterialComponent>(entity);
    materialComp.material = tag.isSkybox ? defaultMat : heroMat;
    materialComp.materialSetIndex = materialComp.material->descriptorSetIndex;
}

std::cout << "[Sentinel] 材质就绪，描述符槽位占用 "
<< descriptorManager->getMaterialSlotUsed() << "/"
<< descriptorManager->getMaterialSlotCapacity() << "\n";
}

void FirstApp::run()
{
	if (screenshotCapture.isEnabled() && screenshotAutoTarget)
		screenshotCapture.setTargetFrame(60);   // 交互模式：预热后第 60 帧
while (!glfwWindowShouldClose(window))
{
glfwPollEvents();
if (!handleWindowMaintenance())
{
ImGui_ImplGlfw_Sleep(10);
continue;
}

auto newTime = std::chrono::high_resolution_clock::now();
float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
currentTime = newTime;

auto updateStart = std::chrono::high_resolution_clock::now();
update(frameTime);
auto updateEnd = std::chrono::high_resolution_clock::now();
render(frameTime);
auto renderEnd = std::chrono::high_resolution_clock::now();
updateMsAccum += std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
renderMsAccum += std::chrono::duration<double, std::milli>(renderEnd - updateEnd).count();
++profiledFrames;

		SENTINEL_PROFILE_FRAME_MARK(); 
// 每 5 秒打一行性能摘要。
// 除了日常观察，这在"无头验证"（重定向 stdout 跑一遍看验证层）时特别有用 ——
// 没有它就无法区分"渲染正常"和"卡死在第一帧"。
++framesSincePerfLog;
auto perfNow = std::chrono::high_resolution_clock::now();
const float perfElapsed = std::chrono::duration<float>(perfNow - lastPerfLog).count();
if (perfElapsed >= 5.0f)
{
const RenderTelemetry& t = scene->getTelemetry();
std::cout << "[Perf] " << static_cast<int>(framesSincePerfLog / perfElapsed) << " FPS"
<< " | draw calls " << t.drawCalls
<< " | desc binds " << t.descriptorBinds
<< " | triangles " << t.triangles
<< " | vertices " << t.vertices
<< " | update " << (updateMsAccum / profiledFrames * 1000.0) << "us"
<< " | render " << (renderMsAccum / profiledFrames * 1000.0) << "us\n";
framesSincePerfLog = 0;
updateMsAccum = 0.0;
renderMsAccum = 0.0;
profiledFrames = 0;
lastPerfLog = perfNow;
}
}
vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void FirstApp::setBloomEnabled(bool enabled)
{
	// 和 ImGui 里那两个复选框走的是完全相同的路径
	scene->getSettings().bloomEnabled = enabled;
}

void FirstApp::setSSAOEnabled(bool enabled)
{
	scene->getSettings().ssaoEnabled = enabled;
}

void FirstApp::setCrossQueueTestEnabled(bool enabled)
{
	crossQueueProbePassPtr->setEnabled(enabled);
}

void FirstApp::saveConfig()
{
	// 相机位姿和渲染参数分两次写，但两次都会"读回已有内容再替换自己那一段"，
	// 所以顺序无关、也不会互相覆盖（见 EngineConfig::save 的说明）。
	EngineConfig::save(scene->getSettings());

	CameraState cam;
	cam.position = scene->getCameraTransform().translation;
	cam.rotation = scene->getCameraTransform().rotation;
	EngineConfig::save(cam);
}

void FirstApp::loadConfig()
{
	EngineConfig::load(scene->getSettings());

	CameraState cam;
	cam.position = scene->getCameraTransform().translation;
	cam.rotation = scene->getCameraTransform().rotation;
	if (EngineConfig::load(cam))
	{
		scene->getCameraTransform().translation = cam.position;
		scene->getCameraTransform().rotation = cam.rotation;
	}

	// 标题是按配置改的，重载之后也要跟着变 —— 否则用户会以为没生效
	glfwSetWindowTitle(window, scene->getSettings().windowTitle.c_str());
}

int FirstApp::runHeadlessSelfTest(int frames)
{
	// 固定帧率推进，不依赖窗口事件循环 —— 这样"关掉某个 Pass 之后验证层是否干净"
	// 也能进自动化回归，而不需要有人去点一下 UI。
	// 每帧仍然走完整的 update + render，包括 ImGui 构建与提交、present。
	suppressAutoSave = true;   // 自检会故意关掉 Pass，别把结果写进用户配置

	if (!jobSystem->selfTest())
		throw std::runtime_error("JobSystem selfTest failed");
	if (!Scene::selfTest())
		throw std::runtime_error("Scene ECS/spatial selfTest failed");

	const EngineSettings& s = scene->getSettings();
	std::cout << "[SelfTest] 开始，共 " << frames << " 帧，bloom=" << (s.bloomEnabled ? 1 : 0)
		<< " ssao=" << (s.ssaoEnabled ? 1 : 0) << "\n";

	materialStressBaselineUsed = descriptorManager->getMaterialSlotUsed();

	if (screenshotCapture.isEnabled() && screenshotAutoTarget)
		screenshotCapture.setTargetFrame(static_cast<uint64_t>(std::min(frames, 60)));

	const auto benchmarkStart = std::chrono::high_resolution_clock::now();
	const float dt = 1.0f / 60.0f;
	for (int i = 0; i < frames; ++i)
	{
		if (resizeTestEnabled)
		{
			switch (i)
			{
			case 5:  glfwSetWindowSize(window, 1280, 720); break;
			case 10: glfwSetWindowSize(window, 800, 600); break;
			case 15: glfwSetWindowSize(window, 1600, 900); break;
			case 20: glfwSetWindowSize(window, WIDTH, HEIGHT); break;
			case 25: setRenderScale(0.5f); break;
			case 35: setRenderScale(1.0f); break;
			case 45: glfwIconifyWindow(window); break;
			case 46: glfwRestoreWindow(window); break;
			default: break;
			}
		}

		if (materialStressEnabled)
			materialStressStep();

		glfwPollEvents();
		if (!handleWindowMaintenance())
		{
			ImGui_ImplGlfw_Sleep(10);
			continue;
		}
		update(dt);
		render(dt);
		SENTINEL_PROFILE_FRAME_MARK();
	}

	const auto benchmarkEnd = std::chrono::high_resolution_clock::now();
	const double benchmarkSeconds = std::chrono::duration<double>(benchmarkEnd - benchmarkStart).count();
	if (benchmarkSeconds > 0.0)
	{
		std::cout << "[SelfTest] performance: " << frames << " frames in "
			<< benchmarkSeconds << " s (" << (frames / benchmarkSeconds) << " FPS)\n";
	}

	if (materialStressEnabled && materialStressPendingSlot != kInvalidMaterialSlot)
	{
		descriptorManager->releaseMaterialSet(materialStressPendingSlot);
		materialStressPendingSlot = kInvalidMaterialSlot;
	}

	vkDeviceWaitIdle(vulkanDevice->getDevice());
	if (materialStressEnabled)
	{
		descriptorManager->collectMaterialGarbage();
		const uint32_t usedAfter = descriptorManager->getMaterialSlotUsed();
		if (usedAfter != materialStressBaselineUsed || descriptorManager->getPendingMaterialReleaseCount() != 0)
			throw std::runtime_error("TD04 material slot stress leaked or failed to retire slots");
		std::cout << "[TD04] material slot stress PASS: cycles=" << materialStressCycles
			<< " used_before=" << materialStressBaselineUsed
			<< " used_after=" << usedAfter
			<< " max_pending=" << materialStressMaxPending
			<< " capacity=" << descriptorManager->getMaterialSlotCapacity() << "\n";
	}

	std::cout << "[SelfTest] 完成 " << frames << " 帧（验证层不再写 stderr；见 [Validation] 摘要 / --validation-jsonl）\n";
	return 0;
}

void FirstApp::update(float dt)
{
	SENTINEL_ZONE_SCOPED("update");
	imguiSystem->newFrame();

	float aspect = vulkanSwapchain->getSwapChainExtent().width /
		(float)vulkanSwapchain->getSwapChainExtent().height;
	scene->update(dt, window, aspect);
}

void FirstApp::render(float dt)
{
	SENTINEL_ZONE_SCOPED("render");
	if (auto commandBuffer = renderer->beginFrame())
	{
		scene->getTelemetry().reset();

		// P1-2：到达目标帧时请求 RenderDoc 抓帧。
		if (renderDoc.shouldCapture(renderer->getFrameNumber()))
			renderDoc.triggerCapture();

		// beginFrame 已确认本 frame slot 的上一轮 GPU 工作完成，可以安全覆写 UBO。
		const uint32_t frameInFlight = renderer->getCurrentFrameInFlightIndex();

		GlobalUbo ubo{};
		ubo.projectionView = scene->getCamera().getProjection() * scene->getCamera().getView();
		ubo.cameraPos = glm::vec4(scene->getCameraTransform().translation, 1.0f);
		ubo.lightDirection = scene->getSettings().directionalLightDir;
		ubo.lightColor = scene->getSettings().directionalLightColor;
		ubo.numLights = 0;
		ubo.exposure = scene->getSettings().exposure;

		memcpy(uboMapped[frameInFlight], &ubo, sizeof(ubo));
		// 持久映射的 UBO 不保证落在 HOST_COHERENT 内存，必须显式 flush，
		// 否则 GPU 可能读到上一帧的旧数据（对 non-coherent 堆是未定义行为）。
		// vmaFlushAllocation 对 coherent 内存是空操作，可以无条件调用。
		vmaFlushAllocation(vulkanDevice->getVmaAllocator(),
			globalUboAllocations[frameInFlight], 0, sizeof(ubo));

		FrameInfo frameInfo{
			frameInFlight,
			renderer->getCurrentImageIndex(),
			renderer->getFrameNumber(),
			dt,
			commandBuffer,
			scene->getSettings(),
			scene->getTelemetry(),
			scene->getRegistry(),
			scene->getVisibleEntities(),
			scene->getCamera().getProjection(),
			scene->getCamera().getView(),
			globalUboBuffers[frameInFlight],
			descriptorManager.get(),
			&scene->getCameraTransform(),
			&scene->getCameraController()
		};

		// W1：把配置存取交给 UI 层。用回调而不是让 ImGuiSystem 持有 FirstApp，
		// 保持"UI 不依赖引擎总控"这条边界。
		frameInfo.onSaveConfig = [this] { saveConfig(); };
		frameInfo.onLoadConfig = [this] { loadConfig(); };

		// D-2：先把本帧的 ImGui 面板建好并生成绘制列表（纯 CPU 工作，不含 GPU 命令）。
		// 放在这里而不是 Present 回调里，是为了让面板上的改动（曝光、Bloom 参数）
		// 当帧就生效 —— 否则会有一帧延迟，拖滑块的手感会发飘。
		imguiSystem->prepareDrawData(frameInfo);

		// D-1(b)：所有 Pass 把自己的工作声明进渲染图，
		// 屏障、布局转换、vkCmdBeginRendering 全部由渲染图负责。
	renderGraph->beginFrame(commandBuffer, renderer->getComputeCommandBuffer(), 		renderer->getCurrentFrameInFlightIndex(), renderer->getFrameNumber());
		renderPipeline.registerAll(*renderGraph, frameInfo);
		// 渲染完成 -> 把这张交换链图像转入 PRESENT_SRC_KHR 供 present
		renderGraph->presentTransition(renderResources->swapchainImages[frameInfo.swapchainImageIndex]);
		renderGraph->endFrame();

		// P1-4：目标帧把交换链图像读回 staging buffer（present 之前）。
		if (screenshotCapture.isTargetFrame(renderer->getFrameNumber()))
			screenshotCapture.recordCopy(commandBuffer, vulkanSwapchain->getImage(frameInfo.swapchainImageIndex),
				vulkanSwapchain->getSwapChainExtent());

		const auto& crossQueue = renderGraph->getCrossQueueDependencies();
		// P1-1：收集本帧的 Tracy GPU zone（必须在命令缓冲结束前）。
		gpuProfiler.collect(commandBuffer);
		renderer->endFrame(crossQueue.computeToGraphics, crossQueue.graphicsToCompute);
		if (screenshotCapture.hasPendingCopy())
		{
			vkDeviceWaitIdle(vulkanDevice->getDevice());
			screenshotCapture.writePng(diagnosticsOptions.screenshotPath);
		}
	}
	else
	{
		// Acquire failed after update() already started an ImGui frame.
		imguiSystem->discardFrame();
	}
}

void FirstApp::finalizeDiagnostics()
{
    if (diagnosticsFinalized) return;
    diagnosticsFinalized = true;

    // 必须在 vkDeviceWaitIdle 之后调用：先读回最后几帧的 GPU 计时，再销毁
    // query pool / staging buffer（此时 device 仍有效）。守卫用的状态必须在
    // destroy() 之前取，否则会被清零而失去意义。
    const bool gpuTimingWasEnabled = gpuTimer.isEnabled();
    const bool screenshotWasEnabled = screenshotCapture.isEnabled();
    const uint32_t gpuTimingQueries = gpuTimer.queryPoolCount();
    const uint32_t screenshotBytes = screenshotCapture.stagedBytes();

#ifdef SENTINEL_ENABLE_GPU_TIMING
    if (gpuTimer.isEnabled())
    {
        gpuTimer.finalize();
        if (gpuTimer.writeCsv(diagnosticsOptions.gpuTimingCsvPath))
            std::cout << "[Diag] gpu_timing.csv written: " << diagnosticsOptions.gpuTimingCsvPath
                      << " (" << gpuTimer.sampleCount() << " samples)\n";
    }
#endif
    gpuTimer.destroy();
    screenshotCapture.destroy();
    renderDoc.destroy();
    gpuProfiler.destroy();

    diag::printValidationSummary();

    // P0-6 零开销守卫：关闭时不应有任何调试用 GPU 资源 / 额外文件。
    const bool anyDebugEnabled = gpuTimingWasEnabled || screenshotWasEnabled;
    const bool guardOk = (gpuTimingQueries == 0 && screenshotBytes == 0);
    std::cout << "[Diag] zero-overhead guard "
              << (anyDebugEnabled ? "SKIP (debug capability on)" : (guardOk ? "PASS" : "FAIL"))
              << " | queryPool=" << gpuTimingQueries
              << " | screenshotBuffer=" << screenshotBytes
              << " | validationJsonl=" << (diagnosticsOptions.validationJsonlPath.empty() ? "off" : "on")
              << "\n";
}