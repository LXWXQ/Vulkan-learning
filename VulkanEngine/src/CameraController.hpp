#pragma once
#include "VulkanGameObject.hpp"
#include <GLFW/glfw3.h>

class CameraController 
{
public:
    struct KeyMappings 
    {
        int moveLeft = GLFW_KEY_A;
        int moveRight = GLFW_KEY_D;
        int moveForward = GLFW_KEY_W;
        int moveBackward = GLFW_KEY_S;
        int moveUp = GLFW_KEY_E;
        int moveDown = GLFW_KEY_Q;
    };

    void processPCInput(GLFWwindow* window, float dt, VulkanGameObject& cameraObj);
    void processAndroidTouchInput(float dt, VulkanGameObject& cameraObj);

private:
    KeyMappings keys{};
    float moveSpeed{5.0f}; // 移动速度
    float lookSpeed{1.0f};  // 鼠标灵敏度
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool isFirstMouse = true;
};