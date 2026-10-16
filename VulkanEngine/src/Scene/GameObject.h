#pragma once
#include "Model.h"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "Core/Material.h"
#include "Core/MaterialSlot.h"
struct TransformComponent 
{
    glm::vec3 translation{}; // 坐标
    glm::vec3 scale{1.f, 1.f, 1.f}; // 缩放
    glm::vec3 rotation{}; // 旋转 (欧拉角)

    glm::mat4 mat4() const 
    {
        glm::mat4 transform = glm::translate(glm::mat4{1.f}, translation);
        transform = glm::rotate(transform, rotation.y, {0.f, 1.f, 0.f});
        transform = glm::rotate(transform, rotation.x, {1.f, 0.f, 0.f});
        transform = glm::rotate(transform, rotation.z, {0.f, 0.f, 1.f});
        transform = glm::scale(transform, scale);
        return transform;
    }
};

class GameObject 
{
public:
    using id_t = unsigned int;

    static GameObject createGameObject() 
    {
        static id_t currentId = 0;
        return GameObject{currentId++};
    }

    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    GameObject(GameObject&&) = default;
    GameObject& operator=(GameObject&&) = default;

    id_t getId() const { return id; }
	std::shared_ptr<Model> model{}; 
	TransformComponent transform{};
	bool isSkybox = false;
	bool isGrid = false;
	std::shared_ptr<Material> material;
    // 未分配时是 kInvalidMaterialSlot。绝不能用 0 —— 0 是合法槽位号，
    // 用 0 当默认值会让没有材质的物体悄悄绑到第 0 号材质上。
    uint32_t materialSetIndex = kInvalidMaterialSlot;
private:
    GameObject(id_t objId) : id{objId} {}
    id_t id;
};