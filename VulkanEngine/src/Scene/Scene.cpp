#include "Scene/Scene.h"
#include "AssetPaths.h"
#include "RHI/Texture.h"
#include "Scene/GLTFLoader.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    constexpr bool kLoadSponza = true;

    entt::entity createTransformEntity(entt::registry& registry)
    {
        const entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity);
        registry.emplace<HierarchyComponent>(entity);
        return entity;
    }

    void setModelBounds(entt::registry& registry, entt::entity entity,
                        const std::shared_ptr<Model>& model)
    {
        if (!model)
            return;

        BoundsComponent& bounds = registry.get_or_emplace<BoundsComponent>(entity);
        bounds.local.min = model->getBoundsMin();
        bounds.local.max = model->getBoundsMax();
        bounds.local.valid = true;
    }

    void updateNodeRecursive(entt::registry& registry, entt::entity entity,
                             const glm::mat4& parentWorld)
    {
        const auto& transform = registry.get<TransformComponent>(entity);
        const glm::mat4 world = parentWorld * transform.localMatrix();

        WorldTransformComponent& worldTransform = registry.get_or_emplace<WorldTransformComponent>(entity);
        worldTransform.matrix = world;
        worldTransform.normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
        worldTransform.worldPosition = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity))
        {
            for (entt::entity child : hierarchy->children)
            {
                if (registry.valid(child))
                    updateNodeRecursive(registry, child, world);
            }
        }
    }

    void updateAllWorldTransforms(entt::registry& registry)
    {
        for (entt::entity entity : registry.view<TransformComponent>())
        {
            const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
            const bool isRoot = !hierarchy || hierarchy->parent == entt::null ||
                                !registry.valid(hierarchy->parent);
            if (isRoot)
                updateNodeRecursive(registry, entity, glm::mat4{ 1.0f });
        }
    }

    struct Plane
    {
        glm::vec3 normal{ 0.0f };
        float distance = 0.0f;
    };

    struct Frustum
    {
        Plane planes[6];
    };

    Plane makePlane(const glm::vec4& equation)
    {
        Plane plane;
        plane.normal = glm::vec3(equation);
        plane.distance = equation.w;
        const float length = glm::length(plane.normal);
        if (length > 0.00001f)
        {
            plane.normal /= length;
            plane.distance /= length;
        }
        return plane;
    }

    Frustum extractFrustum(const glm::mat4& viewProjection)
    {
        const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
        const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
        const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]);
        const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);

        Frustum frustum;
        // Vulkan clip space: -w <= x,y <= w and 0 <= z <= w.
        frustum.planes[0] = makePlane(row3 + row0); // left
        frustum.planes[1] = makePlane(row3 - row0); // right
        frustum.planes[2] = makePlane(row3 + row1); // bottom
        frustum.planes[3] = makePlane(row3 - row1); // top
        frustum.planes[4] = makePlane(row2);        // near
        frustum.planes[5] = makePlane(row3 - row2); // far
        return frustum;
    }

    bool intersectsAABB(const Frustum& frustum, const AABB& bounds)
    {
        if (!bounds.valid)
            return false;

        for (const Plane& plane : frustum.planes)
        {
            const glm::vec3 positive{
                plane.normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
                plane.normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
                plane.normal.z >= 0.0f ? bounds.max.z : bounds.min.z,
            };

            if (glm::dot(plane.normal, positive) + plane.distance < 0.0f)
                return false;
        }
        return true;
    }

    AABB frustumBroadBounds(const glm::mat4& viewProjection)
    {
        const glm::mat4 inverse = glm::inverse(viewProjection);
        AABB bounds;
        bounds.valid = true;

        bool first = true;
        for (int z = 0; z <= 1; ++z)
        {
            for (int y = -1; y <= 1; y += 2)
            {
                for (int x = -1; x <= 1; x += 2)
                {
                    glm::vec4 p = inverse * glm::vec4(static_cast<float>(x), static_cast<float>(y),
                                                      static_cast<float>(z), 1.0f);
                    p /= p.w;
                    const glm::vec3 world(p);
                    if (first)
                    {
                        bounds.min = bounds.max = world;
                        first = false;
                    }
                    else
                    {
                        bounds.min = glm::min(bounds.min, world);
                        bounds.max = glm::max(bounds.max, world);
                    }
                }
            }
        }
        return bounds;
    }

    bool isDescendant(entt::registry& registry, entt::entity candidate, entt::entity ancestor)
    {
        entt::entity current = candidate;
        while (current != entt::null && registry.valid(current))
        {
            if (current == ancestor)
                return true;
            const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
            current = hierarchy ? hierarchy->parent : entt::null;
        }
        return false;
    }
}

void Scene::init(Device& device, VkCommandPool commandPool, DescriptorManager& descriptorManager)
{
    const std::string basePath = SENTINEL_RESOURCE_DIR;

    materialSystem.initDefaultTextures(device, commandPool);

    cameraEntity = createTransformEntity(registry);
    registry.emplace<CameraComponent>(cameraEntity);

    std::shared_ptr<Model> heroModel;
    if (kLoadSponza)
    {
        sentinel::GLTFLoadContext ctx;
        ctx.device = &device;
        ctx.commandPool = commandPool;
        ctx.materialSystem = &materialSystem;
        ctx.descriptorManager = &descriptorManager;

        heroModel = sentinel::loadGLTFModel(ctx, basePath + "Models/Sponza/glTF/Sponza.gltf");
        if (!heroModel)
            std::cerr << "[Scene] Sponza load failed, falling back to DamagedHelmet\n";
    }

    if (heroModel)
    {
        const entt::entity hero = createTransformEntity(registry);
        registry.emplace<MeshComponent>(hero, MeshComponent{ heroModel });
        registry.emplace<RenderTagComponent>(hero);
        setModelBounds(registry, hero, heroModel);

        const glm::vec3 center = heroModel->getBoundsCenter();
        const float radius = heroModel->getBoundsRadius();
        std::cout << "[Scene] Sponza bounds min(" << heroModel->getBoundsMin().x << ", "
                  << heroModel->getBoundsMin().y << ", " << heroModel->getBoundsMin().z
                  << ") max(" << heroModel->getBoundsMax().x << ", "
                  << heroModel->getBoundsMax().y << ", " << heroModel->getBoundsMax().z
                  << ") center(" << center.x << ", " << center.y << ", " << center.z
                  << ") radius " << radius << "\n";

        getCameraTransform().translation = center - glm::vec3(0.0f, 0.0f, radius * 1.6f);
    }
    else
    {
        const entt::entity helmet = createTransformEntity(registry);
        auto helmetModel = Model::createModelFromFile(device, commandPool,
            basePath + "Models/DamagedHelmet/DamagedHelmet.obj");
        registry.emplace<MeshComponent>(helmet, MeshComponent{ helmetModel });
        registry.emplace<RenderTagComponent>(helmet);
        setModelBounds(registry, helmet, helmetModel);
        getCameraTransform().translation = { 0.0f, 0.0f, -5.0f };
    }

    // Skybox: small sphere; the vertex shader performs depth/w translation.
    {
        const entt::entity skybox = createTransformEntity(registry);
        auto skyboxModel = Model::createModelFromFile(device, commandPool,
            basePath + "Models/sphere.obj");
        registry.emplace<MeshComponent>(skybox, MeshComponent{ skyboxModel });
        RenderTagComponent tag;
        tag.isSkybox = true;
        registry.emplace<RenderTagComponent>(skybox, tag);
        registry.get<TransformComponent>(skybox).scale = { 0.1f, 0.1f, 0.1f };
        setModelBounds(registry, skybox, skyboxModel);
    }

    updateAllWorldTransforms(registry);
    rebuildSpatialIndex();
}

entt::entity Scene::createEntity()
{
    return createTransformEntity(registry);
}

void Scene::destroyEntity(entt::entity entity)
{
    if (!registry.valid(entity))
        return;

    if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity))
    {
        if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent))
        {
            auto& siblings = registry.get<HierarchyComponent>(hierarchy->parent).children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        }

        for (entt::entity child : hierarchy->children)
        {
            if (registry.valid(child))
            {
                auto* childHierarchy = registry.try_get<HierarchyComponent>(child);
                if (childHierarchy)
                    childHierarchy->parent = entt::null;
            }
        }
    }

    registry.destroy(entity);
}

void Scene::setParent(entt::entity child, entt::entity parent)
{
    if (!registry.valid(child))
        throw std::runtime_error("Scene::setParent: invalid child entity");
    if (parent != entt::null && !registry.valid(parent))
        throw std::runtime_error("Scene::setParent: invalid parent entity");
    if (child == parent)
        throw std::runtime_error("Scene::setParent: entity cannot parent itself");
    if (parent != entt::null && isDescendant(registry, parent, child))
        throw std::runtime_error("Scene::setParent: cycle detected");

    auto& childHierarchy = registry.get_or_emplace<HierarchyComponent>(child);
    if (childHierarchy.parent != entt::null && registry.valid(childHierarchy.parent))
    {
        auto& oldChildren = registry.get<HierarchyComponent>(childHierarchy.parent).children;
        oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), child), oldChildren.end());
    }

    childHierarchy.parent = parent;
    if (parent != entt::null)
        registry.get_or_emplace<HierarchyComponent>(parent).children.push_back(child);
}

void Scene::update(float dt, GLFWwindow* window, float aspectRatio)
{
    TransformComponent& cameraTransform = getCameraTransform();
    cameraController.processPCInput(window, dt, cameraTransform);

    camera.setViewYXZ(cameraTransform.translation, cameraTransform.rotation);
    camera.setPerspectiveProjection(glm::radians(60.0f), aspectRatio, 0.1f, 5000.0f);

    updateWorldTransforms();
    rebuildSpatialIndex();
    updateVisibility(camera.getProjection() * camera.getView());
}

void Scene::updateWorldTransforms()
{
    updateAllWorldTransforms(registry);
}

void Scene::rebuildSpatialIndex()
{
    spatialIndex.clear();

    for (entt::entity entity : registry.view<MeshComponent>())
    {
        const MeshComponent& mesh = registry.get<MeshComponent>(entity);
        if (!mesh.model)
            continue;

        // Skybox is camera-following and always rendered. Its 1000-unit source
        // sphere would otherwise insert a huge number of grid cells every frame.
        const auto* tag = registry.try_get<RenderTagComponent>(entity);
        if (tag && tag->isSkybox)
            continue;

        auto& bounds = registry.get_or_emplace<BoundsComponent>(entity);
        if (!bounds.local.valid)
        {
            bounds.local.min = mesh.model->getBoundsMin();
            bounds.local.max = mesh.model->getBoundsMax();
            bounds.local.valid = true;
        }

        const WorldTransformComponent& world = registry.get_or_emplace<WorldTransformComponent>(entity);
        WorldBoundsComponent& worldBounds = registry.get_or_emplace<WorldBoundsComponent>(entity);
        worldBounds.world = transformAABB(bounds.local, world.matrix);
        spatialIndex.insert(entity, worldBounds.world);
    }
}

void Scene::updateVisibility(const glm::mat4& viewProjection)
{
    visibleEntities.clear();

    const Frustum frustum = extractFrustum(viewProjection);
    const AABB broadBounds = frustumBroadBounds(viewProjection);

    for (auto [entity, mesh, tag] : registry.view<MeshComponent, RenderTagComponent>().each())
    {
        if (tag.isSkybox)
            visibleEntities.push_back(entity);
    }

    std::vector<entt::entity> candidates;
    spatialIndex.query(broadBounds, candidates);

    for (entt::entity entity : candidates)
    {
        const auto* tag = registry.try_get<RenderTagComponent>(entity);
        const auto* worldBounds = registry.try_get<WorldBoundsComponent>(entity);
        if (!worldBounds)
            continue;

        // Skybox is camera-following and always needed as the background.
        if ((tag && tag->isSkybox) || intersectsAABB(frustum, worldBounds->world))
            visibleEntities.push_back(entity);
    }
}

void Scene::cleanup()
{
    materialSystem.cleanup();
    registry.clear();
    spatialIndex.clear();
    visibleEntities.clear();
    cameraEntity = entt::null;
}

bool Scene::selfTest()
{
    bool ok = true;

    entt::registry registry;
    const entt::entity parent = createTransformEntity(registry);
    const entt::entity child = createTransformEntity(registry);
    const entt::entity grandchild = createTransformEntity(registry);

    registry.get<TransformComponent>(parent).translation = { 1.0f, 2.0f, 3.0f };
    registry.get<TransformComponent>(child).translation = { 0.0f, 1.0f, 0.0f };
    registry.get<TransformComponent>(grandchild).translation = { 0.0f, 0.0f, 2.0f };

    registry.get<HierarchyComponent>(parent).children.push_back(child);
    registry.get<HierarchyComponent>(child).parent = parent;
    registry.get<HierarchyComponent>(child).children.push_back(grandchild);
    registry.get<HierarchyComponent>(grandchild).parent = child;

    updateAllWorldTransforms(registry);

    const glm::vec3 childPos = registry.get<WorldTransformComponent>(child).worldPosition;
    const glm::vec3 grandchildPos = registry.get<WorldTransformComponent>(grandchild).worldPosition;
    if (glm::length(childPos - glm::vec3(1.0f, 3.0f, 3.0f)) > 0.0001f ||
        glm::length(grandchildPos - glm::vec3(1.0f, 3.0f, 5.0f)) > 0.0001f)
    {
        std::cerr << "[Scene] selfTest FAIL: hierarchy world transform mismatch\n";
        ok = false;
    }

    SpatialIndex index(4.0f);
    const entt::entity nearEntity = registry.create();
    const entt::entity farEntity = registry.create();
    index.insert(nearEntity, AABB{ { 0.0f, 0.0f, 0.0f }, { 3.0f, 3.0f, 3.0f }, true });
    index.insert(farEntity, AABB{ { 100.0f, 100.0f, 100.0f }, { 104.0f, 104.0f, 104.0f }, true });

    std::vector<entt::entity> result;
    index.query(AABB{ { 1.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, true }, result);
    if (result.size() != 1 || result[0] != nearEntity)
    {
        std::cerr << "[Scene] selfTest FAIL: spatial index query mismatch\n";
        ok = false;
    }

    std::cout << "[Scene] ECS/spatial selfTest " << (ok ? "PASS" : "FAIL")
              << " | hierarchy=T*Ry*Rx*Rz*S | grid cells=" << index.cellCount() << "\n";
    return ok;
}