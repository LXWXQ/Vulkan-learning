#include "CameraController.h"
#include <algorithm>
#include <glm/gtc/constants.hpp>

void CameraController::setMode(Mode m)
{
	if (mode != m)
		firstMouse = true;
	mode = m;
}

void CameraController::processPCInput(GLFWwindow* window, float dt, GameObject& cameraObj)
{
	bool rightPressed  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)  == GLFW_PRESS;
	bool middlePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
	bool dragging = rightPressed || middlePressed;

	if (rightPressed)
		setMode(Mode::FPS);
	else if (middlePressed)
		setMode(Mode::Orbit);

	if (!dragging)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		firstMouse = true;

		// 没有拖动时，把 transform 当作权威值同步回内部状态 ——
		// ImGui 面板可以直接改 transform.translation / rotation，
		// 如果不做这个同步，下一次按下鼠标时陈旧的 smooth* 会把镜头"拉回"旧位置。
		smoothPosition = cameraObj.transform.translation;
		yaw   = smoothYaw   = cameraObj.transform.rotation.y;
		pitch = smoothPitch = glm::clamp(cameraObj.transform.rotation.x,
			-glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);

		// 同步轨道焦点，避免切到 Orbit 模式时镜头跳回旧焦点
		const glm::vec3 fwd{ sin(smoothYaw) * cos(smoothPitch),
			sin(smoothPitch),
			cos(smoothYaw) * cos(smoothPitch) };
		focusPoint = smoothPosition + fwd * distance;
		return;
	}

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	double mx, my;
	glfwGetCursorPos(window, &mx, &my);
	if (firstMouse)
	{
		lastMouseX = mx;
		lastMouseY = my;
		firstMouse = false;
	}

	float dx = static_cast<float>(mx - lastMouseX);
	float dy = static_cast<float>(my - lastMouseY);
	lastMouseX = mx;
	lastMouseY = my;

	// Orbit 模式下视角转动减半，手感更稳
	const float sensitivity = (mode == Mode::FPS) ? lookSensitivity : lookSensitivity * 0.5f;
	yaw   -= dx * sensitivity;
	pitch -= dy * sensitivity;
	pitch = glm::clamp(pitch, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);

	float effectiveSpeed = moveSpeed;
	if (glfwGetKey(window, keys.sprint) == GLFW_PRESS)
		effectiveSpeed *= sprintMultiplier;

	// 阻尼插值：smoothEnabled 关闭时 t = 1，等价于直接使用原始值。
	// 注意这里每一帧都会收敛，所以松开鼠标之后镜头依然会平滑到位
	//（原实现只在按住鼠标时更新，松开后 smooth* 永远停在中途，下次拖动会跳一下）。
	const float t = smoothEnabled ? (1.0f - glm::exp(-smoothFactor * dt)) : 1.0f;
	smoothYaw   += (yaw - smoothYaw) * t;
	smoothPitch += (pitch - smoothPitch) * t;

	glm::vec3 forward{ sin(smoothYaw) * cos(smoothPitch),
		sin(smoothPitch),
		cos(smoothYaw) * cos(smoothPitch) };
	glm::vec3 right{ forward.z, 0.0f, -forward.x };
	glm::vec3 up{ 0.0f, -1.0f, 0.0f };

	if (mode == Mode::FPS)
	{
		glm::vec3 moveDir{ 0.0f };
		if (glfwGetKey(window, keys.moveForward)  == GLFW_PRESS) moveDir += forward;
		if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS) moveDir -= forward;
		if (glfwGetKey(window, keys.moveRight)    == GLFW_PRESS) moveDir += right;
		if (glfwGetKey(window, keys.moveLeft)     == GLFW_PRESS) moveDir -= right;
		if (glfwGetKey(window, keys.moveUp)       == GLFW_PRESS) moveDir += up;
		if (glfwGetKey(window, keys.moveDown)     == GLFW_PRESS) moveDir -= up;

		if (glm::dot(moveDir, moveDir) > 0.0001f)
			smoothPosition += effectiveSpeed * dt * glm::normalize(moveDir);

		focusPoint = smoothPosition + forward * distance;
	}
	else
	{
		smoothPosition = focusPoint - forward * distance;
	}

	// 写回"实际使用的"已平滑值，保证 ImGui 显示的数字和画面一致
	cameraObj.transform.translation = smoothPosition;
	cameraObj.transform.rotation = { smoothPitch, smoothYaw, 0.0f };
}

void CameraController::onScroll(double, double yoffset)
{
	if (mode == Mode::Orbit)
	{
		distance -= static_cast<float>(yoffset) * scrollSpeed * 0.1f;
		distance = glm::clamp(distance, minDistance, maxDistance);
	}
	else
	{
		moveSpeed += static_cast<float>(yoffset) * 2.0f;
		moveSpeed = glm::clamp(moveSpeed, 0.5f, 50.0f);
	}
}

static CameraController* s_activeController = nullptr;

static void scrollCallback(GLFWwindow*, double x, double y)
{
	if (s_activeController)
		s_activeController->onScroll(x, y);
}

void CameraController::registerCallbacks(GLFWwindow* window)
{
	s_activeController = this;
	glfwSetScrollCallback(window, scrollCallback);
}
