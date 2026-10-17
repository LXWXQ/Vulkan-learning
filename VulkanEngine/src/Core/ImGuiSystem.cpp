#include "ImGuiSystem.h"
#include "Scene/CameraController.h"
#include "Core/EngineConfig.h"
#include <stdexcept>

ImGuiSystem::ImGuiSystem(GLFWwindow* window, Device& device, VkFormat swapchainFormat, uint32_t imageCount, VkCommandPool commandPool)
	: vulkanDevice(device), swapchainFormat(swapchainFormat)
{
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	vkCreateDescriptorPool(device.getDevice(), &pool_info, nullptr, &imguiPool);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = device.getInstance();
	init_info.PhysicalDevice = device.getPhysicalDevice();
	init_info.Device = device.getDevice();
	init_info.QueueFamily = device.findPhysicalQueueFamilies().graphicsFamily.value();
	init_info.Queue = device.getGraphicsQueue();
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 2;
	init_info.ImageCount = imageCount;
	// 动态渲染：告诉 ImGui 后端不要用 VkRenderPass，改为声明颜色附件格式
	init_info.UseDynamicRendering = true;
	// 注意：这个版本(1.92.6)里字段在 PipelineInfoMain 下，不是顶层
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &this->swapchainFormat;
	ImGui_ImplVulkan_Init(&init_info);
}

ImGuiSystem::~ImGuiSystem()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(vulkanDevice.getDevice(), imguiPool, nullptr);
}

void ImGuiSystem::newFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void ImGuiSystem::prepareDrawData(FrameInfo& frameInfo)
{
	// 让面板宽度严格贴合内容：ImGui 的窗口宽度是"历史最大值"，一旦某帧出现过
	// 宽内容（比如一长串路径），窗口就再也不会缩回来，在画面上永久留一块
	// 多余的黑底把场景挡住。每帧清一次尺寸标志，面板宽度就只由当前内容决定。
	// 高度同理 —— 面板变矮时下面不留空块。
	ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);

	ImGui::Begin("Sentinel Engine Core");

	renderPerformancePanel(frameInfo.frameTime, frameInfo.telemetry, frameInfo.settings);
	renderSSAOPanel(frameInfo.settings);
	renderLightingPanel(frameInfo.settings);
	renderBloomPanel(frameInfo.settings);

	if (frameInfo.cameraTransform)
		renderCameraPanel(*frameInfo.cameraTransform, frameInfo.cameraController);

	renderConfigPanel(frameInfo);

	ImGui::End();
	ImGui::Render();
}

void ImGuiSystem::renderDrawData(VkCommandBuffer commandBuffer)
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiSystem::discardFrame()
{
	ImGui::EndFrame();
}

void ImGuiSystem::renderPerformancePanel(float dt, RenderTelemetry& telemetry, EngineSettings& settings)
{
	if (!ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
	ImGui::Text("Draw Calls : %u", telemetry.drawCalls);
	ImGui::Text("Triangles  : %u", telemetry.triangles);
	ImGui::Text("Vertices   : %u", telemetry.vertices);
	// D-3 bindless 的收益只体现在这一行：draw call 数不变，但绑定次数从
	// "每个子网格一次"降到"每帧一次"。看着不起眼，它是后面实例化的前提。
	ImGui::Text("Desc. Binds: %u", telemetry.descriptorBinds);
	ImGui::SliderFloat("Render Scale", &settings.renderScale, 0.25f, 1.0f, "%.2f");
}

void ImGuiSystem::renderSSAOPanel(EngineSettings& settings)
{
	if (!ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Checkbox("Enable SSAO", &settings.ssaoEnabled);

	if (settings.ssaoEnabled)
	{
		ImGui::Indent();
		ImGui::SliderFloat("Radius", &settings.ssaoRadius, 0.1f, 3.0f, "%.2f");
		ImGui::SliderFloat("Bias", &settings.ssaoBias, 0.0f, 0.2f, "%.3f");
		ImGui::SliderInt("Samples", &settings.ssaoKernelSize, 4, 64);
		ImGui::Unindent();
	}
}

void ImGuiSystem::renderLightingPanel(EngineSettings& settings)
{
	if (!ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f);
	ImGui::SliderFloat3("Sun Direction", &settings.directionalLightDir.x, -1.0f, 1.0f);
	ImGui::ColorEdit3("Sun Color", &settings.directionalLightColor.x);
}

void ImGuiSystem::renderBloomPanel(EngineSettings& settings)
{
	if (!ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Checkbox("Enable Bloom", &settings.bloomEnabled);

	if (!settings.bloomEnabled)
		return;

	ImGui::Indent();
	// 阈值作用在线性 HDR 上：1.0 附近 ≈ "刚好纯白"，调高只让最强的过曝区起辉光
	ImGui::SliderFloat("Threshold", &settings.bloomThreshold, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat("Soft Knee", &settings.bloomKnee, 0.01f, 2.0f, "%.2f");
	ImGui::SliderFloat("Intensity", &settings.bloomIntensity, 0.0f, 0.5f, "%.3f");
	ImGui::Unindent();
}

void ImGuiSystem::renderCameraPanel(TransformComponent& cameraTransform, CameraController* controller)
{
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::DragFloat3("Position", &cameraTransform.translation.x, 0.1f);

	glm::vec3 eulerAngles = glm::degrees(cameraTransform.rotation);
	if (ImGui::DragFloat3("Rotation", &eulerAngles.x, 1.0f))
		cameraTransform.rotation = glm::radians(eulerAngles);

	if (!controller) return;

	ImGui::Separator();

	const char* modeName = (controller->getMode() == CameraController::Mode::FPS) ? "FPS" : "Orbit";
	ImGui::Text("Mode: %s  (RMB=FPS / MMB=Orbit)", modeName);

	ImGui::SliderFloat("Move Speed", &controller->moveSpeed, 0.5f, 50.0f, "%.1f");
	ImGui::SliderFloat("Sprint Boost", &controller->sprintMultiplier, 1.5f, 10.0f, "%.1fx");
	ImGui::SliderFloat("Look Sensitivity", &controller->lookSensitivity, 0.01f, 1.0f, "%.2f");
	ImGui::SliderFloat("Scroll Speed", &controller->scrollSpeed, 1.0f, 50.0f, "%.1f");
	ImGui::Checkbox("Smooth Camera", &controller->smoothEnabled);
	if (controller->smoothEnabled)
	{
		ImGui::Indent();
		ImGui::SliderFloat("Smooth Factor", &controller->smoothFactor, 1.0f, 30.0f, "%.1f");
		ImGui::Unindent();
	}
}

void ImGuiSystem::renderConfigPanel(FrameInfo& frameInfo)
{
	if (!ImGui::CollapsingHeader("Config", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	const std::string& fullPath = EngineConfig::getConfigPath();

	// 只显示文件名，完整路径放进 tooltip。
	//
	// 为什么：完整路径是绝对路径，可能有五十多个字符 —— ImGui 的窗口宽度是
	// 由内容撑开的，一行长文本会把整个面板拉宽一大截（实测从 283px 变成 583px），
	// 把画面左上角一大块都盖住。而且这个工程的路径里带中文目录名，
	// 默认字体没有对应字形，会渲染成一串 "???"。
	const size_t slash = fullPath.find_last_of("/\\");
	const std::string fileName = (slash == std::string::npos) ? fullPath : fullPath.substr(slash + 1);

	ImGui::TextUnformatted(("File: " + fileName).c_str());
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", fullPath.c_str());

	// 两个按钮各占一半宽度。注意 ImGui 的按钮文字如果超出按钮宽度会被裁掉，
	// 所以宽度按可用区域算，不要写死。
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

	if (ImGui::Button("Save", ImVec2(half, 0.0f)) && frameInfo.onSaveConfig)
		frameInfo.onSaveConfig();

	ImGui::SameLine();

	if (ImGui::Button("Reload", ImVec2(half, 0.0f)) && frameInfo.onLoadConfig)
		frameInfo.onLoadConfig();

	ImGui::TextDisabled("(exit also saves)");

	// 上一次读/写的结果。放在这里而不是只打控制台，是因为"点了保存但没生效"
	// 这类问题必须在眼睛能看到的地方回答，不能要求用户去翻日志。
	// 状态里可能带路径，所以让它自动换行，避免又把面板撑宽。
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(EngineConfig::getLastStatus().c_str());
	ImGui::PopTextWrapPos();
}
