#include "Scene/Scene.h"
#include "AssetPaths.h"
#include "RHI/Texture.h"
#include "Scene/GLTFLoader.h"
#include <iostream>

namespace
{
// 场景开关：想切回 DamagedHelmet 演示就改成 false
constexpr bool kLoadSponza = true;
}

void Scene::init(Device& device, VkCommandPool commandPool, DescriptorManager& descriptorManager)
{
const std::string basePath = SENTINEL_RESOURCE_DIR;

materialSystem.initDefaultTextures(device, commandPool);

// 天空盒用的球（顺带充当调试模型）
quadSphereModel = Model::createModelFromFile(device, commandPool, basePath + "Models/debug_cube.obj");

if (kLoadSponza)
{
sentinel::GLTFLoadContext ctx;
ctx.device = &device;
ctx.commandPool = commandPool;
ctx.materialSystem = &materialSystem;
ctx.descriptorManager = &descriptorManager;

std::shared_ptr<Model> sponza = sentinel::loadGLTFModel(
ctx, basePath + "Models/Sponza/glTF/Sponza.gltf");

if (sponza)
{
GameObject hero = GameObject::createGameObject();
hero.model = sponza;
// 材质槽位已经由 glTF 加载器逐子网格烘焙进 SubMesh::materialSlot，
// 所以这里刻意保持"未分配"，让渲染侧走子网格自带的材质。
hero.materialSetIndex = kInvalidMaterialSlot;
gameObjects.push_back(std::move(hero));

// 用包围盒把相机摆到场景外侧，保证一进来就看得见东西
const glm::vec3 bmin = sponza->getBoundsMin();
const glm::vec3 bmax = sponza->getBoundsMax();
const glm::vec3 center = sponza->getBoundsCenter();
const float radius = sponza->getBoundsRadius();
std::cout << "[Scene] Sponza 包围盒 min(" << bmin.x << ", " << bmin.y << ", " << bmin.z
<< ") max(" << bmax.x << ", " << bmax.y << ", " << bmax.z
<< ") 中心(" << center.x << ", " << center.y << ", " << center.z
<< ") 半径 " << radius << "\n";

// 相机放在中心 -Z 方向：setViewYXZ 在 rotation = 0 时朝向 +Z
cameraObject.transform.translation = center - glm::vec3(0.0f, 0.0f, radius * 1.6f);
}
else
{
std::cerr << "[Scene] Sponza 加载失败，回退到 DamagedHelmet 演示场景\n";
}
}

// 兜底：没加载到主模型就用 DamagedHelmet
bool hasHero = false;
for (const auto& obj : gameObjects)
if (obj.model && !obj.isSkybox) hasHero = true;

if (!hasHero)
{
GameObject helmet = GameObject::createGameObject();
helmet.model = Model::createModelFromFile(device, commandPool,
basePath + "Models/DamagedHelmet/DamagedHelmet.obj");
helmet.transform.translation = { 0.0f, 0.0f, 0.0f };
helmet.transform.scale = { 1.0f, 1.0f, 1.0f };
gameObjects.push_back(std::move(helmet));

cameraObject.transform.translation = { 0.0f, 0.0f, -5.0f };
}

// 天空盒：小半径球体 + 跟随相机（顶点着色器里 gl_Position = pos.xyww）
{
GameObject skybox = GameObject::createGameObject();
skybox.model = Model::createModelFromFile(device, commandPool, basePath + "Models/sphere.obj");
skybox.transform.scale = { 0.1f, 0.1f, 0.1f };
skybox.isSkybox = true;
gameObjects.push_back(std::move(skybox));
}
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
