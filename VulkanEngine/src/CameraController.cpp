#include "CameraController.hpp"
#include <algorithm>
#include <iostream>

void CameraController::processPCInput(GLFWwindow* window, float dt, VulkanGameObject& cameraObj) {
    // 1. 鼠标视角控制 (按住右键时才允许转动视角，松开时可以去点 ImGui)
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // 隐藏光标，无限滑动
        
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        if (isFirstMouse) {
            lastMouseX = xpos;
            lastMouseY = ypos;
            isFirstMouse = false;
        }

        float xoffset = static_cast<float>(xpos - lastMouseX);
        float yoffset = static_cast<float>(ypos - lastMouseY);
        lastMouseX = xpos;
        lastMouseY = ypos;

        cameraObj.transform.rotation.y += xoffset * lookSpeed * dt;
        cameraObj.transform.rotation.x -= yoffset * lookSpeed * dt; // Y轴向下所以用减号

        // 限制抬头/低头的角度 (-85度到85度左右)，防止万向节死锁
        cameraObj.transform.rotation.x = glm::clamp(cameraObj.transform.rotation.x, -1.5f, 1.5f);
        cameraObj.transform.rotation.y = glm::mod(cameraObj.transform.rotation.y, glm::two_pi<float>());
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // 释放鼠标给 ImGui
        isFirstMouse = true;
    }

    // 2. 键盘移动控制 (经典的 FPS 漫游)
    float yaw = cameraObj.transform.rotation.y;
    const glm::vec3 forwardDir{sin(yaw), 0.f, cos(yaw)};
    const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
    const glm::vec3 upDir{0.f, -1.f, 0.f};

    glm::vec3 moveDir{0.f};
    if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir += forwardDir;
    if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS) moveDir -= forwardDir;
    if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
    if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
    if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
    if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;

    if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
        cameraObj.transform.translation += moveSpeed * dt * glm::normalize(moveDir);
    }
}

void CameraController::processAndroidTouchInput(float dt, VulkanGameObject& cameraObj) {
    // 哨兵预留防线：此处目前为空。
    // 未来移植到 Android 时，解析从 JNI 传来的双指触控坐标，
    // 将滑动差值映射到 cameraObj.transform.rotation，
    // 将虚拟摇杆的推力映射到 cameraObj.transform.translation 即可。
}