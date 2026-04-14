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
    createDescriptorPool();
    

    // 🌟 组装流水线 (直接把 globalSetLayout 喂给 Pass)
    auto geoPass = std::make_unique<GeometryPass>(*vulkanDevice, *gBuffer, globalSetLayout);
    auto ssaoPass = std::make_unique<SSAOPass>(*vulkanDevice, extent, renderer->getCommandPool(), globalSetLayout);
    auto lightPass = std::make_unique<LightingPass>(*vulkanDevice, *gBuffer, *vulkanSwapchain, globalSetLayout);
    
    LightingPass* lightPassPtr = lightPass.get(); 

    renderPipeline.addPass(std::move(geoPass));    // 第 1 步：画出 G-Buffer (Pos, Normal, 深度等)
    renderPipeline.addPass(std::move(ssaoPass));   // 第 2 步：读取 G-Buffer，算出 SSAO 遮蔽图
    renderPipeline.addPass(std::move(lightPass));  // 第 3 步：读取 G-Buffer 和 SSAO，合成最终光照上屏！

    // 初始化时，Pass 内部会自动创建 RenderPass 并顺手实例化对应的 System！
    renderPipeline.initAll();

    renderTargets["SSAO"] = {
        ssaoPass->getOutputSampler(), 
        ssaoPass->getOutputImageView(), 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    createDescriptorSets();

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
    vkDestroyDescriptorPool(vulkanDevice->getDevice(), descriptorPool, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();
}


void FirstApp::createRenderTargets()
{
     
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

        // 1. 计算这一帧花了多少时间 (Delta Time)
        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        // 2. 逻辑更新 (Tick)
        update(frameTime);

        // 3. 画面渲染 (Draw)
        render(frameTime);
    }
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}


void FirstApp::update(float dt) 
{
    imguiSystem->newFrame(); // ImGui 的数据更新也属于逻辑层

#ifdef __ANDROID__
    cameraController.processAndroidTouchInput(dt, cameraObject);
#else
    cameraController.processPCInput(window, dt, cameraObject);
#endif

    // 更新相机矩阵
    camera.setViewYXZ(cameraObject.transform.translation, cameraObject.transform.rotation);
    float aspect = vulkanSwapchain->getSwapChainExtent().width / (float)vulkanSwapchain->getSwapChainExtent().height;
    camera.setPerspectiveProjection(glm::radians(60.f), aspect, 0.1f, 5000.f);

    // 更新游戏物体（比如旋转中间的茶壶）
    static float accumulatedTime = 0.0f;
    accumulatedTime += dt;
    if (!gameObjects.empty()) 
    {
        gameObjects[0].transform.rotation.y = accumulatedTime * glm::radians(45.0f);
    }

    // 💡 注意：这里没有一行 vk 开头的代码！
}

void FirstApp::render(float dt) 
{
    if (auto commandBuffer = renderer->beginFrame()) 
    {
        uint32_t imageIndex = renderer->getCurrentImageIndex();

        // 1. 构建并拷贝 UBO 数据到 GPU
        GlobalUbo ubo{};
        ubo.projectionView = camera.getProjection() * camera.getView();
        ubo.cameraPos = glm::vec4(cameraObject.transform.translation, 1.0f);
        ubo.numLights = MAX_POINT_LIGHTS;

        static float lightTime = 0.0f;
        lightTime += dt;

        for (int i = 0; i < ubo.numLights; i++) 
        {
            float angle = i * glm::two_pi<float>() / 100.0f + lightTime / 5.0f;
            float radius = 5.0f;
            float height = -10.0f + (i % 10) * 2.0f;
            ubo.pointLights[i].position = glm::vec4(cos(angle) * radius, height, sin(angle) * radius, 15.0f);

            float colorAngle = (i / 100.0f) * 3.14159f; 
            ubo.pointLights[i].color = glm::vec4(
                glm::abs(sin(colorAngle * 2.0f)), 
                glm::abs(cos(colorAngle * 0.5f)), 
                1.0f - glm::abs(sin(colorAngle)), 
                50.0f
            );
        }
        memcpy(uboMapped, &ubo, sizeof(ubo));

        FrameInfo frameInfo{
            imageIndex,
            dt,          
            commandBuffer,
            globalDescriptorSet,
            gameObjects,
            camera.getProjection(), // 🌟 传入投影矩阵
            camera.getView()
        };

        // 2. 🌟 全自动流水线发车 🌟
        for (auto& pass : renderPipeline.getPasses()) 
        {
            pass->execute(commandBuffer, frameInfo);
        }

        // 3. 结束这一帧
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
        samplerBinding.binding = i; // binding 分别为 1, 2, 3, 4, 5
        samplerBinding.descriptorCount = 1;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.pImmutableSamplers = nullptr;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // 只有片段着色器需要贴图
        bindings.push_back(samplerBinding);
    }

    for (uint32_t i = 6; i <= 10; i++) 
    {
        VkDescriptorSetLayoutBinding inputBinding{};
        inputBinding.binding = i;
        inputBinding.descriptorCount = 1;
        inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // 关键类型！
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

void FirstApp::createDescriptorPool() 
{
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) 
    {
        throw std::runtime_error("无法创建 PBR 描述符池！");
    }
}

void FirstApp::createDescriptorSets() 
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &globalSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &allocInfo, &globalDescriptorSet) != VK_SUCCESS) 
    {
        throw std::runtime_error("无法分配 PBR 描述符集！");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = globalUboBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GlobalUbo);

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

    std::array<VkWriteDescriptorSet, 11> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = globalDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    auto writeImage = [&](uint32_t binding, VkDescriptorImageInfo* imgInfo) 
    {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = globalDescriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = imgInfo;
        return write;
    };

    descriptorWrites[1] = writeImage(1, &albedoInfo);
    descriptorWrites[2] = writeImage(2, &normalInfo);
    descriptorWrites[3] = writeImage(3, &metallicInfo);
    descriptorWrites[4] = writeImage(4, &roughnessInfo);
    descriptorWrites[5] = writeImage(5, &environmentInfo);

    auto writeInput = [&](uint32_t binding, VkDescriptorImageInfo* imgInfo) 
    {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = globalDescriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; 
        write.descriptorCount = 1;
        write.pImageInfo = imgInfo;
        return write;
    };

    descriptorWrites[6] = writeInput(6, &posInputInfo);
    descriptorWrites[7] = writeInput(7, &normalInputInfo);
    descriptorWrites[8] = writeInput(8, &albedoInputInfo);
    descriptorWrites[9] = writeInput(9, &pbrInputInfo);
    descriptorWrites[10] = writeInput(10, &ssaoInfo);
    vkUpdateDescriptorSets(vulkanDevice->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void FirstApp::loadAllPBRTextures() 
{
    std::string basePath = "../../Resources/Texture/"; // ⚠️ 请核对你的绝对路径！
    albedoTex = std::make_unique<Texture>(*vulkanDevice, basePath + "RuslLessRL/albedo.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    normalTex = std::make_unique<Texture>(*vulkanDevice, basePath + "RuslLessRL/normal.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    metallicTex = std::make_unique<Texture>(*vulkanDevice, basePath + "RuslLessRL/metallic.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());
    roughnessTex = std::make_unique<Texture>(*vulkanDevice, basePath + "RuslLessRL/roughness.jpg", VK_FORMAT_R8G8B8A8_UNORM, renderer->getCommandPool());

    std::cout << "[Sentinel 通报] 4 发 PBR 贴图已全部成功轰入 GPU 核心金库！\n";
    std::cout << "[Sentinel 通报] 正在装填 HDR 环境辐射图...\n";
    environmentTex = std::make_unique<Texture>(*vulkanDevice, basePath + "environment.hdr", VK_FORMAT_R32G32B32A32_SFLOAT, renderer->getCommandPool(), true);
}

void FirstApp::loadGameObjects() 
{
    quadSphereModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/debug_cube.obj");
    std::shared_ptr<Model> teapotModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/Quad-Sphere.obj");
    
    GameObject teapot = GameObject::createGameObject();
    teapot.model = teapotModel; // 挂载模型组件
    teapot.transform.rotation = {0.0f, 0.0f, 0.0f}; // 挂载旋转组件，顺时针旋转 180 度，面向正 Z 轴
    teapot.transform.translation = {0.0f, 0.0f, 0.0f}; // 挂载位置组件
    teapot.transform.scale = {1.0f, 1.0f, 1.0f};       // 挂载缩放组件
    
    gameObjects.push_back(std::move(teapot)); // 扔进世界


    std::shared_ptr<Model> planeModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/plane.obj");
    
    GameObject grid = GameObject::createGameObject();
    grid.model = planeModel;
    grid.isGrid = true;
    grid.transform.translation = {0.0f, -100.0f, 0.0f}; // 稍微往下放一点，垫在茶壶下面
    grid.transform.scale = {1000.0f, 1000.0f, 1000.0f};    // 铺开 100 倍
    
    gameObjects.push_back(std::move(grid));

    std::shared_ptr<Model> sphereModel = Model::createModelFromFile(*vulkanDevice, "../../Resources/Models/sphere.obj");
    
    GameObject skybox = GameObject::createGameObject();
    skybox.model = sphereModel;
    skybox.transform.scale = {0.1f, 0.1f, 0.1f}; // 放大到足够包裹整个场景的尺寸
    skybox.isSkybox = true; // 挂载天空盒特征标签
    
    gameObjects.push_back(std::move(skybox)); // 扔进世界
}
