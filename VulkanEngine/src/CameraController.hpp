#pragma once
#include "VulkanGameObject.hpp"
#include <GLFW/glfw3.h>

class CameraController {
public:
    struct KeyMappings {
        int moveLeft = GLFW_KEY_A;
        int moveRight = GLFW_KEY_D;
        int moveForward = GLFW_KEY_W;
        int moveBackward = GLFW_KEY_S;
        int moveUp = GLFW_KEY_E;
        int moveDown = GLFW_KEY_Q;
    };

    // =======================================================
    // 跨平台输入分发总线
    // =======================================================
    
    // PC 端：传统原生 GLFW 键鼠捕获
    void processPCInput(GLFWwindow* window, float dt, VulkanGameObject& cameraObj);

    // Android 端：预留虚拟触摸板接口 (后续由 Android NDK/JNI 传入触控事件)
    // TODO: 未来可在此处传入结构体，例如 TouchEvent(x, y, isPressed)
    void processAndroidTouchInput(float dt, VulkanGameObject& cameraObj);

private:
    KeyMappings keys{};
    float moveSpeed{5.0f}; // 移动速度
    float lookSpeed{1.0f};  // 鼠标灵敏度

    // PC 端鼠标状态缓存
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool isFirstMouse = true;
};