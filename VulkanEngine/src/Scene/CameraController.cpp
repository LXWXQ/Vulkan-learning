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
	bool rightPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool middlePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

	if (rightPressed)
		setMode(Mode::FPS);
	else if (middlePressed)
		setMode(Mode::Orbit);
	else
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		firstMouse = true;
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

	if (mode == Mode::FPS)
	{
		yaw -= dx * lookSensitivity;
		pitch -= dy * lookSensitivity;
	}
	else
	{
		yaw -= dx * lookSensitivity * 0.5f;
		pitch -= dy * lookSensitivity * 0.5f;
	}

	pitch = glm::clamp(pitch, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);

	float effectiveSpeed = moveSpeed;
	if (glfwGetKey(window, keys.sprint) == GLFW_PRESS)
		effectiveSpeed *= sprintMultiplier;

	if (mode == Mode::FPS)
	{
		float currentYaw = smoothEnabled ? smoothYaw : yaw;
		float currentPitch = smoothEnabled ? smoothPitch : pitch;

		glm::vec3 forward{ sin(currentYaw) * cos(currentPitch), sin(currentPitch), cos(currentYaw) * cos(currentPitch) };
		glm::vec3 right{ forward.z, 0.0f, -forward.x };
		glm::vec3 up{ 0.0f, -1.0f, 0.0f };

		glm::vec3 moveDir{ 0.0f };
		if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS)  moveDir += forward;
		if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS) moveDir -= forward;
		if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS)    moveDir += right;
		if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS)     moveDir -= right;
		if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS)       moveDir += up;
		if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS)     moveDir -= up;

		if (glm::dot(moveDir, moveDir) > 0.0001f)
			smoothPosition += effectiveSpeed * dt * glm::normalize(moveDir);

		focusPoint = smoothPosition + forward * distance;

		if (smoothEnabled)
		{
			float t = 1.0f - glm::exp(-smoothFactor * dt);
			smoothYaw += (yaw - smoothYaw) * t;
			smoothPitch += (pitch - smoothPitch) * t;
		}
		else
		{
			smoothYaw = yaw;
			smoothPitch = pitch;
		}
	}
	else
	{
		float currentYaw = smoothEnabled ? smoothYaw : yaw;
		float currentPitch = smoothEnabled ? smoothPitch : pitch;

		glm::vec3 forward{ sin(currentYaw) * cos(currentPitch), sin(currentPitch), cos(currentYaw) * cos(currentPitch) };
		smoothPosition = focusPoint - forward * distance;

		if (smoothEnabled)
		{
			float t = 1.0f - glm::exp(-smoothFactor * dt);
			smoothYaw += (yaw - smoothYaw) * t;
			smoothPitch += (pitch - smoothPitch) * t;
		}
		else
		{
			smoothYaw = yaw;
			smoothPitch = pitch;
		}
	}

	cameraObj.transform.translation = smoothPosition;
	cameraObj.transform.rotation = { pitch, yaw, 0.0f };
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
