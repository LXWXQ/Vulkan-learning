#pragma once
#include "GameObject.h"
#include <GLFW/glfw3.h>

class CameraController
{
public:
	enum class Mode { FPS, Orbit };

	struct KeyMappings
	{
		int moveLeft = GLFW_KEY_A;
		int moveRight = GLFW_KEY_D;
		int moveForward = GLFW_KEY_W;
		int moveBackward = GLFW_KEY_S;
		int moveUp = GLFW_KEY_E;
		int moveDown = GLFW_KEY_Q;
		int sprint = GLFW_KEY_LEFT_SHIFT;
	};

	void processPCInput(GLFWwindow* window, float dt, GameObject& cameraObj);
	void processAndroidTouchInput(float dt, GameObject& cameraObj) {}
	void onScroll(double xoffset, double yoffset);
	void registerCallbacks(GLFWwindow* window);

	void setMode(Mode m);
	Mode getMode() const { return mode; }
	void setFocusPoint(const glm::vec3& point) { focusPoint = point; }

	float moveSpeed = 5.0f;
	float sprintMultiplier = 3.0f;
	float lookSensitivity = 0.01f;
	float orbitSpeed = 2.0f;
	float scrollSpeed = 10.0f;
	bool smoothEnabled = true;
	float smoothFactor = 10.0f;
	float minDistance = 0.5f;
	float maxDistance = 500.0f;
	KeyMappings keys{};

private:
	Mode mode = Mode::FPS;

	float yaw = 0.0f;
	float pitch = 0.0f;
	float distance = 5.0f;
	glm::vec3 focusPoint{ 0.0f, 0.0f, 0.0f };
	glm::vec3 smoothPosition{ 0.0f, 0.0f, -5.0f };
	float smoothYaw = 0.0f;
	float smoothPitch = 0.0f;

	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	bool firstMouse = true;
};
