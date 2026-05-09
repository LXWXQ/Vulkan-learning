#include "Scene/Scene.h"
#include "RHI/Texture.h"
#include <iostream>

void Scene::init(Device& device, VkCommandPool commandPool)
{
	std::string basePath = "../../Resources/";

	materialSystem.initDefaultTextures(device, commandPool);

	quadSphereModel = Model::createModelFromFile(device, basePath + "Models/debug_cube.obj");

	std::shared_ptr<Model> helmetModel = Model::createModelFromFile(device, basePath + "Models/DamagedHelmet/DamagedHelmet.obj");
	GameObject helmet = GameObject::createGameObject();
	helmet.model = helmetModel;
	helmet.transform.translation = { 0.0f, 0.0f, 0.0f };
	helmet.transform.scale = { 1.0f, 1.0f, 1.0f };
	gameObjects.push_back(std::move(helmet));

	std::shared_ptr<Model> planeModel = Model::createModelFromFile(device, basePath + "Models/plane.obj");
	GameObject grid = GameObject::createGameObject();
	grid.model = planeModel;
	grid.isGrid = true;
	grid.transform.translation = { 0.0f, -100.0f, 0.0f };
	grid.transform.scale = { 1000.0f, 1000.0f, 1000.0f };
	gameObjects.push_back(std::move(grid));

	std::shared_ptr<Model> sphereModel = Model::createModelFromFile(device, basePath + "Models/sphere.obj");
	GameObject skybox = GameObject::createGameObject();
	skybox.model = sphereModel;
	skybox.transform.scale = { 0.1f, 0.1f, 0.1f };
	skybox.isSkybox = true;
	gameObjects.push_back(std::move(skybox));

	cameraObject.transform.translation = { 0.0f, 0.0f, -5.0f };
}

void Scene::update(float dt, GLFWwindow* window, float aspectRatio)
{
	cameraController.processPCInput(window, dt, cameraObject);
	camera.setViewYXZ(cameraObject.transform.translation, cameraObject.transform.rotation);
	camera.setPerspectiveProjection(glm::radians(60.f), aspectRatio, 0.1f, 5000.f);
}

void Scene::cleanup()
{
	materialSystem.cleanup();
	gameObjects.clear();
}
