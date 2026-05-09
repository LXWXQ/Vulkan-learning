#pragma once
#include "Scene/GameObject.h"
#include "Scene/Model.h"
#include "Scene/Camera.h"
#include "Scene/CameraController.h"
#include "Core/MaterialSystem.h"
#include "Core/FrameInfo.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>

class Scene
{
public:
	void init(Device& device, VkCommandPool commandPool);
	void update(float dt, GLFWwindow* window, float aspectRatio);
	void cleanup();

	std::vector<GameObject>& getGameObjects() { return gameObjects; }
	VulkanCamera& getCamera() { return camera; }
	CameraController& getCameraController() { return cameraController; }
	EngineSettings& getSettings() { return engineSettings; }
	RenderTelemetry& getTelemetry() { return currentTelemetry; }
	MaterialSystem& getMaterialSystem() { return materialSystem; }

	std::shared_ptr<Model> getQuadSphereModel() const { return quadSphereModel; }

	GameObject cameraObject = GameObject::createGameObject();

private:
	std::vector<GameObject> gameObjects;
	VulkanCamera camera{};
	CameraController cameraController{};

	std::shared_ptr<Model> quadSphereModel;

	EngineSettings engineSettings;
	RenderTelemetry currentTelemetry;
	MaterialSystem materialSystem;
};
