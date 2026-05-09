#pragma once
#include <vulkan/vulkan.h>
#include "Scene/Model.h"
#include "Scene/GameObject.h"
#include <glm/glm.hpp>

#define MAX_POINT_LIGHTS 100
#define WIDTH 1920
#define HEIGHT 1080

class DescriptorManager;
class CameraController;

struct PointLight
{
	glm::vec4 position{};
	glm::vec4 color{};
};

struct GlobalUbo
{
	glm::mat4 projectionView{ 1.f };

	alignas(16) glm::vec4 ambientLightColor{ 1.f, 1.f, 1.f, .05f };
	alignas(16) glm::vec4 lightDirection{ 1.f, 1.f, 1.f, 0.f };
	alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
	alignas(16) glm::vec4 cameraPos{ 0.f };

	alignas(16) int numLights = 0;
	alignas(16) PointLight pointLights[MAX_POINT_LIGHTS];
};

struct SimplePushConstantData
{
	glm::mat4 modelMatrix{ 1.f };
	glm::mat4 normalMatrix{ 1.f };
};

struct EngineSettings
{
	bool ssaoEnabled = true;
	float ssaoRadius = 0.5f;
	float ssaoBias = 0.025f;
	int ssaoKernelSize = 16;

	bool ssrEnabled = false;
	bool shadowEnabled = false;

	glm::vec4 directionalLightDir{ -1.0f, -1.0f, 1.0f, 0.0f };
	glm::vec4 directionalLightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	float exposure = 1.0f;
};

struct RenderTelemetry
{
	uint32_t drawCalls = 0;
	uint32_t triangles = 0;
	uint32_t vertices = 0;

	void reset()
	{
		drawCalls = 0;
		triangles = 0;
		vertices = 0;
	}
};

struct FrameInfo
{
	uint32_t frameIndex;
	float frameTime;
	VkCommandBuffer commandBuffer;

	EngineSettings& settings;
	RenderTelemetry& telemetry;
	std::vector<GameObject>& gameObjects;

	glm::mat4 projectionMatrix;
	glm::mat4 viewMatrix;

	VkBuffer globalUboBuffer;
	DescriptorManager* descriptorManager = nullptr;
	GameObject* cameraObj = nullptr;
	CameraController* cameraController = nullptr;
};
