#pragma once

#include "Scene/Components.h"
#include "Scene/Camera.h"
#include "Scene/CameraController.h"
#include "Scene/SpatialIndex.h"
#include "Core/MaterialSystem.h"
#include "Core/DescriptorManager.h"
#include "Core/FrameInfo.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"

#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <memory>
#include <vector>

class Scene
{
public:
    // DescriptorManager is required because glTF materials must reserve
    // bindless material slots while the scene is being loaded.
    void init(Device& device, VkCommandPool commandPool, DescriptorManager& descriptorManager);
    void update(float dt, GLFWwindow* window, float aspectRatio);
    void cleanup();

    entt::entity createEntity();
    void destroyEntity(entt::entity entity);

    // Reparenting keeps HierarchyComponent::children consistent and rejects
    // cycles. Passing entt::null makes the entity a root.
    void setParent(entt::entity child, entt::entity parent);

    entt::registry& getRegistry() { return registry; }
    const entt::registry& getRegistry() const { return registry; }
    const std::vector<entt::entity>& getVisibleEntities() const { return visibleEntities; }

    entt::entity getCameraEntity() const { return cameraEntity; }
    TransformComponent& getCameraTransform() { return registry.get<TransformComponent>(cameraEntity); }
    const TransformComponent& getCameraTransform() const { return registry.get<TransformComponent>(cameraEntity); }

    VulkanCamera& getCamera() { return camera; }
    CameraController& getCameraController() { return cameraController; }
    EngineSettings& getSettings() { return engineSettings; }
    RenderTelemetry& getTelemetry() { return currentTelemetry; }
    const RenderTelemetry& getDisplayTelemetry() const { return displayTelemetry; }

    // Called once at the start of a render frame, before the UI is built.
    // The previous frame's counts are complete and safe to display while the
    // current frame is still being recorded.
    void beginFrameTelemetry()
    {
        displayTelemetry = currentTelemetry;
        currentTelemetry.reset();
    }

    MaterialSystem& getMaterialSystem() { return materialSystem; }

    // D-7 regression test: hierarchy transform propagation + uniform-grid query.
    static bool selfTest();

private:
    void updateWorldTransforms();
    void rebuildSpatialIndex();
    void updateVisibility(const glm::mat4& viewProjection);

    entt::registry registry;
    entt::entity cameraEntity = entt::null;
    SpatialIndex spatialIndex{ 16.0f };
    std::vector<entt::entity> visibleEntities;

    VulkanCamera camera{};
    CameraController cameraController{};

    EngineSettings engineSettings;
    RenderTelemetry currentTelemetry;
    RenderTelemetry displayTelemetry;
    MaterialSystem materialSystem;
};