#include "Application.h"
#include "Core/FrameInfo.h"
#include "Render/GeometryPass.h"
#include "Render/LightingPass.h"
#include "Render/SSAOPass.h"
#include <stdexcept>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

FirstApp::FirstApp()
{
	initWindow();
	vulkanDevice = std::make_unique<Device>(window);

	VkExtent2D extent{ WIDTH, HEIGHT };
	vulkanSwapchain = std::make_unique<Swapchain>(*vulkanDevice, extent);
	renderer = std::make_unique<Renderer>(*vulkanDevice, *vulkanSwapchain);
	gBuffer = std::make_unique<GBuffer>(*vulkanDevice, extent);

	scene = std::make_unique<Scene>();
	scene->init(*vulkanDevice, renderer->getCommandPool());
	scene->getCameraController().registerCallbacks(window);

	descriptorManager = std::make_unique<DescriptorManager>();
	descriptorManager->init(*vulkanDevice, 256);

	loadAllPBRTextures();
	createUniformBuffers();
	initMaterials();

	auto geoPass = std::make_unique<GeometryPass>(*vulkanDevice, *gBuffer,
		descriptorManager->getGlobalSetLayout(), descriptorManager->getMaterialSetLayout());
	auto ssaoPass = std::make_unique<SSAOPass>(*vulkanDevice, extent, renderer->getCommandPool(),
		descriptorManager->getGlobalSetLayout(), descriptorManager.get());
	auto lightPass = std::make_unique<LightingPass>(*vulkanDevice, *gBuffer, *vulkanSwapchain,
		descriptorManager->getGlobalSetLayout());

	SSAOPass* ssaoPassPtr = ssaoPass.get();
	LightingPass* lightPassPtr = lightPass.get();

	renderPipeline.addPass(std::move(geoPass));
	renderPipeline.addPass(std::move(ssaoPass));
	renderPipeline.addPass(std::move(lightPass));

	renderPipeline.initAll();

	VkDescriptorImageInfo envInfo{ environmentTex->getSampler(), environmentTex->getView(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	descriptorManager->buildGlobalSet(globalUboBuffer, envInfo, *gBuffer,
		ssaoPassPtr->getOutputSampler(), ssaoPassPtr->getOutputImageView());

	imguiSystem = std::make_unique<ImGuiSystem>(window, *vulkanDevice,
		lightPassPtr->getRenderPass(), vulkanSwapchain->imageCount(), renderer->getCommandPool());
	lightPassPtr->setImGuiSystem(imguiSystem.get());
	lightPassPtr->setDebugModel(scene->getQuadSphereModel());

	currentTime = std::chrono::high_resolution_clock::now();
}

FirstApp::~FirstApp()
{
	vmaDestroyBuffer(vulkanDevice->getVmaAllocator(), globalUboBuffer, globalUboAllocation);
	descriptorManager->cleanup();
	scene->cleanup();
	glfwDestroyWindow(window);
	glfwTerminate();
}

void FirstApp::initWindow()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Engine - Sentinel", nullptr, nullptr);
}

void FirstApp::createUniformBuffers()
{
	VkDeviceSize bufferSize = sizeof(GlobalUbo);
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	vmaCreateBuffer(vulkanDevice->getVmaAllocator(), &bufferInfo, &allocInfo,
		&globalUboBuffer, &globalUboAllocation, nullptr);

	VmaAllocationInfo info;
	vmaGetAllocationInfo(vulkanDevice->getVmaAllocator(), globalUboAllocation, &info);
	uboMapped = info.pMappedData;
}

void FirstApp::loadAllPBRTextures()
{
	std::string basePath = "../../Resources/";
	auto& matSys = scene->getMaterialSystem();

	matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_albedo.jpg",
		*vulkanDevice, renderer->getCommandPool());
	matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_normal.jpg",
		*vulkanDevice, renderer->getCommandPool());
	matSys.loadTexture(basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg",
		*vulkanDevice, renderer->getCommandPool());

	environmentTex = std::make_unique<Texture>(*vulkanDevice,
		basePath + "Texture/environment.hdr",
		VK_FORMAT_R32G32B32A32_SFLOAT, renderer->getCommandPool(), true);

	std::cout << "[Sentinel] PBR textures cached + HDR environment loaded.\n";
}

void FirstApp::initMaterials()
{
	auto& matSys = scene->getMaterialSystem();
	std::string basePath = "../../Resources/";

	auto helmetMat = matSys.getOrCreateMaterial("DamagedHelmet");
	helmetMat->albedoMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_albedo.jpg");
	helmetMat->normalMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_normal.jpg");
	helmetMat->metallicRoughnessMap = matSys.getTexture(basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg");

	matSys.buildMaterialDescriptor(helmetMat, *descriptorManager);

	for (auto& obj : scene->getGameObjects())
	{
		if (!obj.isSkybox && !obj.isGrid)
		{
			obj.material = helmetMat;
			obj.materialSetIndex = helmetMat->descriptorSetIndex;
		}
	}

	std::cout << "[Sentinel] Material 'DamagedHelmet' built, descriptor slot "
		<< helmetMat->descriptorSetIndex << ".\n";
}

void FirstApp::run()
{
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
		{
			ImGui_ImplGlfw_Sleep(10);
			continue;
		}

		auto newTime = std::chrono::high_resolution_clock::now();
		float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
		currentTime = newTime;

		update(frameTime);
		render(frameTime);
	}
	vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void FirstApp::update(float dt)
{
	imguiSystem->newFrame();

	float aspect = vulkanSwapchain->getSwapChainExtent().width /
		(float)vulkanSwapchain->getSwapChainExtent().height;
	scene->update(dt, window, aspect);
}

void FirstApp::render(float dt)
{
	if (auto commandBuffer = renderer->beginFrame())
	{
		scene->getTelemetry().reset();

		GlobalUbo ubo{};
		ubo.projectionView = scene->getCamera().getProjection() * scene->getCamera().getView();
		ubo.cameraPos = glm::vec4(scene->cameraObject.transform.translation, 1.0f);
		ubo.lightDirection = scene->getSettings().directionalLightDir;
		ubo.lightColor = scene->getSettings().directionalLightColor;
		ubo.numLights = 0;

		memcpy(uboMapped, &ubo, sizeof(ubo));

		FrameInfo frameInfo{
			renderer->getCurrentImageIndex(),
			dt,
			commandBuffer,
			scene->getSettings(),
			scene->getTelemetry(),
			scene->getGameObjects(),
			scene->getCamera().getProjection(),
			scene->getCamera().getView(),
			globalUboBuffer,
			descriptorManager.get(),
			&scene->cameraObject,
			&scene->getCameraController()
		};

		for (auto& pass : renderPipeline.getPasses())
			pass->execute(commandBuffer, frameInfo);

		renderer->endFrame();
	}
}
