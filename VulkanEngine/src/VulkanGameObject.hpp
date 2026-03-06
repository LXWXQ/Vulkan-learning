#pragma once
#include "VulkanModel.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

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

class VulkanGameObject 
{
public:
    using id_t = unsigned int;

    static VulkanGameObject createGameObject() 
    {
        static id_t currentId = 0;
        return VulkanGameObject{currentId++};
    }

    VulkanGameObject(const VulkanGameObject&) = delete;
    VulkanGameObject& operator=(const VulkanGameObject&) = delete;
    VulkanGameObject(VulkanGameObject&&) = default;
    VulkanGameObject& operator=(VulkanGameObject&&) = default;

    id_t getId() const { return id; }
    std::shared_ptr<VulkanModel> model{}; 
    TransformComponent transform{};
    bool isSkybox = false;
    bool isGrid = false;

private:
    VulkanGameObject(id_t objId) : id{objId} {}
    id_t id;
};