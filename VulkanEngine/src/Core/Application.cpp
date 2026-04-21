#include "Application.h"
#include <stdexcept>
#include <array>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

FirstApp::FirstApp() 
{
    initWindow();
    vulkanDevice = std::make_unique<Device>(window);
    loadGameObjects();
    
    VkExtent2D extent{WIDTH, HEIGHT};
    vulkanSwapchain = std::make_unique<Swapchain>(*vulkanDevice, extent);
    renderer = std::make_unique<Renderer>(*vulkanDevice, *vulkanSwapchain);
    gBuffer = std::make_unique<GBuffer>(*vulkanDevice, vulkanSwapchain->getSwapChainExtent());
    
    createDescriptorSetLayout();
    loadAllPBRTextures(); 
    createUniformBuffers();

    // 🌟 彻底移除了旧版的 createDescriptorPool 和 createDescriptorSets！
    
    // 🌟 为每一帧配置专属的动态大管家
    frameDescriptorAllocators.resize(vulkanSwapchain->imageCount());
    for (int i = 0; i < frameDescriptorAllocators.size(); ++i) 
    {
        frameDescriptorAllocators[i] = std::make_unique<DescriptorAllocator>();
        frameDescriptorAllocators[i]->init(vulkanDevice->getDevice());
    }

    auto geoPass = std::make_unique<GeometryPass>(*vulkanDevice, *gBuffer, globalSetLayout);
    std::cout << "GeometryPass 创建成功\n";
    auto ssaoPass = std::make_unique<SSAOPass>(*vulkanDevice, extent, renderer->getCommandPool(), globalSetLayout);
    std::cout << "SSAOPass 创建成功\n";

    auto lightPass = std::make_unique<LightingPass>(*vulkanDevice, *gBuffer, *vulkanSwapchain, globalSetLayout);
    std::cout << "LightingPass 创建成功\n";

    LightingPass* lightPassPtr = lightPass.get(); 
    SSAOPass* ssaoPassPtr = ssaoPass.get();
    
    renderPipeline.addPass(std::move(geoPass));    
    renderPipeline.addPass(std::move(ssaoPass));   
    renderPipeline.addPass(std::move(lightPass));  

    renderPipeline.initAll();

    renderTargets["SSAO"] = {ssaoPassPtr->getOutputSampler(), ssaoPassPtr->getOutputImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    // 初始化 ImGui
    imguiSystem = std::make_unique<ImGuiSystem>(window, *vulkanDevice, lightPassPtr->getRenderPass(), vulkanSwapchain->imageCount(), renderer->getCommandPool());
    lightPassPtr->setImGuiSystem(imguiSystem.get());
    lightPassPtr->setDebugModel(quadSphereModel);

    cameraObject.transform.translation = {0.0f, 0.0f, -5.0f};
    currentTime = std::chrono::high_resolution_clock::now();
}

FirstApp::~FirstApp() 
{
    vkDestroyDescriptorSetLayout(vulkanDevice->getDevice(), globalSetLayout, nullptr);
    vkDestroyBuffer(vulkanDevice->getDevice(), globalUboBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), globalUboBufferMemory, nullptr);
    
    // 🌟 释放所有动态分配器
    for (auto& alloc : frameDescriptorAllocators) 
    {
        alloc->cleanup();
    }
    
    glfwDestroyWindow(window);
    glfwTerminate();
}

void FirstApp::initWindow() 
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Engine - Sentinel", nullptr, nullptr);
}

void FirstApp::run() 
{
    while (!glfwWindowShouldClose(window)) 
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) 
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        update(frameTime);
        render(frameTime);
    }
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void FirstApp::update(float dt) 
{
    imguiSystem->newFrame();

#ifdef __ANDROID__
    cameraController.processAndroidTouchInput(dt, cameraObject);
#else
    cameraController.processPCInput(window, dt, cameraObject);
#endif

    camera.setViewYXZ(cameraObject.transform.translation, cameraObject.transform.rotation);
    float aspect = vulkanSwapchain->getSwapChainExtent().width / (float)vulkanSwapchain->getSwapChainExtent().height;
    camera.setPerspectiveProjection(glm::radians(60.f), aspect, 0.1f, 5000.f);

    static float accumulatedTime = 0.0f;
    accumulatedTime += dt;
    if (!gameObjects.empty()) 
    {
        //gameObjects[0].transform.rotation.y = accumulatedTime * glm::radians(45.0f);
    }
}

void FirstApp::render(float dt) 
{
    if (auto commandBuffer = renderer->beginFrame()) 
    {
        uint32_t imageIndex = renderer->getCurrentImageIndex();

        // 🌟 核心引擎心跳：帧首瞬间清空上一轮的描述符池，绝不侧漏！
        frameDescriptorAllocators[imageIndex]->resetPools();
        currentTelemetry.reset();
        GlobalUbo ubo{};
        ubo.projectionView = camera.getProjection() * camera.getView();
        ubo.cameraPos = glm::vec4(cameraObject.transform.translation, 1.0f);

        ubo.lightDirection = engineSettings.directionalLightDir;
        ubo.lightColor = engineSettings.directionalLightColor;

        ubo.numLights = MAX_POINT_LIGHTS;

        static float lightTime = 0.0f;
        lightTime += dt;

        for (int i = 0; i < ubo.numLights; i++) 
        {
            float angle = i * glm::two_pi<float>() / 100.0f + lightTime / 5.0f;
            float radius = 5.0f;
            float height = -10.0f + (i % 10) * 2.0f;
            ubo.pointLights[i].position = glm::vec4(cos(angle) * radius, height, sin(angle) * radius, 15.0f);
            ubo.pointLights[i].color =glm::vec4(0.0f);
            float colorAngle = (i / 100.0f) * 3.14159f; 
           /* ubo.pointLights[i].color = glm::vec4(
                glm::abs(sin(colorAngle * 2.0f)), 
                glm::abs(cos(colorAngle * 0.5f)), 
                1.0f - glm::abs(sin(colorAngle)), 
                50.0f
            );*/
        }
        memcpy(uboMapped, &ubo, sizeof(ubo));

        // 🌟 准备好所有资源的信息包
        VkDescriptorImageInfo albedoInfo{albedoTex->getSampler(), albedoTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normalInfo{normalTex->getSampler(), normalTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo metallicInfo{metallicTex->getSampler(), metallicTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo roughnessInfo{roughnessTex->getSampler(), roughnessTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo environmentInfo{environmentTex->getSampler(), environmentTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkDescriptorImageInfo posInputInfo{gBuffer->getSampler(), gBuffer->getPosition().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normalInputInfo{gBuffer->getSampler(), gBuffer->getNormal().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo albedoInputInfo{gBuffer->getSampler(), gBuffer->getAlbedo().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo pbrInputInfo{gBuffer->getSampler(), gBuffer->getPbr().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkDescriptorImageInfo ssaoInfo = renderTargets["SSAO"];
        VkDescriptorImageInfo dummyInfo = albedoInfo; // 使用反照率作为安全的替身

        // 🌟 满载资源的 FrameInfo 发车！
        FrameInfo frameInfo{
            imageIndex,
            dt,          
            commandBuffer,
            frameDescriptorAllocators[imageIndex].get(),
            engineSettings,
            currentTelemetry,
            gameObjects,
            camera.getProjection(), 
            camera.getView(),
            // --- 资源总线装填 ---
            globalUboBuffer,
            globalSetLayout,
            albedoInfo, normalInfo, metallicInfo, roughnessInfo, environmentInfo,
            posInputInfo, normalInputInfo, albedoInputInfo, pbrInputInfo,
            ssaoInfo, dummyInfo
        };

        for (auto& pass : renderPipeline.getPasses()) 
        {
            pass->execute(commandBuffer, frameInfo);
        }

        renderer->endFrame();
    }
}

void FirstApp::createDescriptorSetLayout()
 {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; 
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding};
    for (uint32_t i = 1; i <= 5; i++) 
    {
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = i; 
        samplerBinding.descriptorCount = 1;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.pImmutableSamplers = nullptr;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; 
        bindings.push_back(samplerBinding);
    }

    for (uint32_t i = 6; i <= 10; i++) 
    {
        VkDescriptorSetLayoutBinding inputBinding{};
        inputBinding.binding = i;
        inputBinding.descriptorCount = 1;
        inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; 
        inputBinding.pImmutableSamplers = nullptr;
        inputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(inputBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice->getDevice(), &layoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS) 
    {
        throw std::runtime_error("无法创建 PBR 描述符集布局！");
    }
}

void FirstApp::createUniformBuffers() 
{
    VkDeviceSize bufferSize = sizeof(GlobalUbo);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice->getDevice(), &bufferInfo, nullptr, &globalUboBuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create uniform buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->getDevice(), globalUboBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &globalUboBufferMemory) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to allocate uniform buffer memory!");
    }

    vkBindBufferMemory(vulkanDevice->getDevice(), globalUboBuffer, globalUboBufferMemory, 0);
    vkMapMemory(vulkanDevice->getDevice(), globalUboBufferMemory, 0, bufferSize, 0, &uboMapped);
}

void FirstApp::loadAllPBRTextures() 
{
    std::string basePath = "../../Resources/"; 
    albedoTex = std::make_unique<Texture>(*vulkanDevice, basePath + "Models/DamagedHelmet/Default_albedo.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    normalTex = std::make_unique<Texture>(*vulkanDevice, basePath + "Models/DamagedHelmet/Default_normal.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    
    // 🌟 核心注意：在 glTF 标准中，金属度和粗糙度是合在同一张图里的！
    // 我们暂时把这张图同时装填进 metallicTex 和 roughnessTex 槽位
    metallicTex = std::make_unique<Texture>(*vulkanDevice, basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    roughnessTex = std::make_unique<Texture>(*vulkanDevice, basePath + "Models/DamagedHelmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());

    std::cout << "[Sentinel 通报] 破损头盔 PBR 贴图装填完毕！\n";

    std::cout << "[Sentinel 通报] 4 发 PBR 贴图已全部成功轰入 GPU 核心金库！\n";
    std::cout << "[Sentinel 通报] 正在装填 HDR 环境辐射图...\n";
    environmentTex = std::make_unique<Texture>(*vulkanDevice, basePath + "Texture/environment.hdr", VK_FORMAT_R32G32B32A32_SFLOAT, renderer->getCommandPool(), true);
}

void FirstApp::loadGameObjects() 
{
    quadSphereModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/debug_cube.obj");
    std::shared_ptr<Model> teapotModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/DamagedHelmet/DamagedHelmet.obj");
    
    GameObject teapot = GameObject::createGameObject();
    teapot.model = teapotModel; 
    teapot.transform.rotation = {0.0f, 0.0f, 0.0f}; 
    teapot.transform.translation = {0.0f, 0.0f, 0.0f}; 
    teapot.transform.scale = {1.0f, 1.0f, 1.0f};       
    
    gameObjects.push_back(std::move(teapot)); 


    std::shared_ptr<Model> planeModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/plane.obj");
    
    GameObject grid = GameObject::createGameObject();
    grid.model = planeModel;
    grid.isGrid = true;
    grid.transform.translation = {0.0f, -100.0f, 0.0f}; 
    grid.transform.scale = {1000.0f, 1000.0f, 1000.0f};    
    
    gameObjects.push_back(std::move(grid));

    std::shared_ptr<Model> sphereModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/sphere.obj");
    
   GameObject skybox = GameObject::createGameObject();
    skybox.model = sphereModel;
    skybox.transform.scale = {0.1f, 0.1f, 0.1f}; 
    skybox.isSkybox = true; 
    
    gameObjects.push_back(std::move(skybox));
}