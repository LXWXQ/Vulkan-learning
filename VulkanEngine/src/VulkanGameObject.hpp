#pragma once
#include "VulkanModel.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

// 1. Component (组件)：变换组件 (控制位置、旋转、缩放)
struct TransformComponent 
{
    glm::vec3 translation{}; // 坐标
    glm::vec3 scale{1.f, 1.f, 1.f}; // 缩放
    glm::vec3 rotation{}; // 旋转 (欧拉角)

    // 核心矩阵运算：将分离的坐标转换为 GPU 需要的 4x4 矩阵
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

// 2. Entity (实体)：游戏对象的载体
class VulkanGameObject 
{
public:
    using id_t = unsigned int;

    // 工厂函数，保证全宇宙每个物体有唯一的 ID
    static VulkanGameObject createGameObject() 
    {
        static id_t currentId = 0;
        return VulkanGameObject{currentId++};
    }

    // 严禁拷贝，防止同样的实体在内存中出现两个分身
    VulkanGameObject(const VulkanGameObject&) = delete;
    VulkanGameObject& operator=(const VulkanGameObject&) = delete;
    VulkanGameObject(VulkanGameObject&&) = default;
    VulkanGameObject& operator=(VulkanGameObject&&) = default;

    id_t getId() const { return id; }

    // --- 挂载的 Components (组件) ---
    // 使用 shared_ptr！这样 100 棵树可以共用同一个模型显存，实现真正的实例化(Instancing)前置准备
    std::shared_ptr<VulkanModel> model{}; 
    TransformComponent transform{};
    bool isSkybox = false; // 简易的 Tag Component (标签组件)

private:
    VulkanGameObject(id_t objId) : id{objId} {}
    id_t id;
};