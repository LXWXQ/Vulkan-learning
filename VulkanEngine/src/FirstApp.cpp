#include "FirstApp.hpp"
#include <stdexcept>
#include <array>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

FirstApp::FirstApp() 
{
    initWindow();
    vulkanDevice = std::make_unique<VulkanDevice>(window);
    loadGameObjects();
    createCommandPool();
    VkExtent2D extent{WIDTH, HEIGHT};
    vulkanSwapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, extent);
    createGBufferResources();
    createRenderPass();
    createFramebuffers();
    createDescriptorSetLayout();
    loadAllPBRTextures(); 
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();

    geometrySystem = std::make_unique<GeometrySystem>(*vulkanDevice, renderPass, globalSetLayout);
    lightingSystem = std::make_unique<LightingSystem>(*vulkanDevice, renderPass, globalSetLayout);
    
    createCommandBuffers();
    createSyncObjects();

    imguiSystem = std::make_unique<ImGuiSystem>(window, *vulkanDevice, renderPass, vulkanSwapchain->imageCount(), commandPool);
    cameraObject.transform.translation = {0.0f, 0.0f, -5.0f};
}

FirstApp::~FirstApp() 
{
    for (auto framebuffer : framebuffers) 
    {
        vkDestroyFramebuffer(vulkanDevice->getDevice(), framebuffer, nullptr);
    }

    auto destroyAttachment = [&](FrameBufferAttachment& attachment) 
    {
        vkDestroyImageView(vulkanDevice->getDevice(), attachment.view, nullptr);
        vkDestroyImage(vulkanDevice->getDevice(), attachment.image, nullptr);
        vkFreeMemory(vulkanDevice->getDevice(), attachment.memory, nullptr);
    };

    destroyAttachment(gBuffer.position);
    destroyAttachment(gBuffer.normal);
    destroyAttachment(gBuffer.albedo);
    destroyAttachment(gBuffer.pbr);
    destroyAttachment(gBuffer.depth);
    vkDestroySampler(vulkanDevice->getDevice(), gBuffer.sampler, nullptr);

    auto destroyTexture = [&](TextureData& tex) 
    {
        vkDestroySampler(vulkanDevice->getDevice(), tex.sampler, nullptr);
        vkDestroyImageView(vulkanDevice->getDevice(), tex.view, nullptr);
        vkDestroyImage(vulkanDevice->getDevice(), tex.image, nullptr);
        vkFreeMemory(vulkanDevice->getDevice(), tex.memory, nullptr);
    };

    destroyTexture(albedoTex);
    destroyTexture(normalTex);
    destroyTexture(metallicTex);
    destroyTexture(roughnessTex);
    destroyTexture(environmentTex);

    vkDestroyDescriptorSetLayout(vulkanDevice->getDevice(), globalSetLayout, nullptr);
    vkDestroyBuffer(vulkanDevice->getDevice(), globalUboBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), globalUboBufferMemory, nullptr);
    vkDestroyDescriptorPool(vulkanDevice->getDevice(), descriptorPool, nullptr);

    vkDestroySemaphore(vulkanDevice->getDevice(), renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(vulkanDevice->getDevice(), imageAvailableSemaphore, nullptr);
    vkDestroyFence(vulkanDevice->getDevice(), inFlightFence, nullptr);
    
    vkDestroyCommandPool(vulkanDevice->getDevice(), commandPool, nullptr);
    vkDestroyRenderPass(vulkanDevice->getDevice(), renderPass, nullptr);
    
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
    VulkanCamera camera{};
    CameraController cameraController{};

    auto currentTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) 
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) 
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        imguiSystem->newFrame();
        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

#ifdef __ANDROID__
        cameraController.processAndroidTouchInput(frameTime, cameraObject);
#else
        cameraController.processPCInput(window, frameTime, cameraObject);
#endif

        camera.setViewYXZ(cameraObject.transform.translation, cameraObject.transform.rotation);
        float aspect = vulkanSwapchain->getSwapChainExtent().width / (float)vulkanSwapchain->getSwapChainExtent().height;
        camera.setPerspectiveProjection(glm::radians(60.f), aspect, 0.1f, 5000.f);

        drawFrame(camera, frameTime);
    }
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void FirstApp::createRenderPass() 
{
    std::array<VkAttachmentDescription, 6> attachments{};
    attachments[0].format = vulkanSwapchain->getSwapChainImageFormat();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    for (int i = 1; i <= 4; i++) 
    {
        attachments[i].format = (i == 1 || i == 2) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; 
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
    }

    attachments[5].format = vulkanDevice->findDepthFormat();
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 4> colorReferences = 
    {
        VkAttachmentReference{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        VkAttachmentReference{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        VkAttachmentReference{3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        VkAttachmentReference{4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference depthReference = {5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription geometrySubpass{};
    geometrySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometrySubpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
    geometrySubpass.pColorAttachments = colorReferences.data();
    geometrySubpass.pDepthStencilAttachment = &depthReference;

    VkAttachmentReference swapchainReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    
    std::array<VkAttachmentReference, 5> inputReferences = 
    {
        VkAttachmentReference{1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{5, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    };

    VkAttachmentReference lightingDepthReference = {5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkSubpassDescription lightingSubpass{};
    lightingSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    lightingSubpass.colorAttachmentCount = 1;
    lightingSubpass.pColorAttachments = &swapchainReference;
    lightingSubpass.inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
    lightingSubpass.pInputAttachments = inputReferences.data();
    lightingSubpass.pDepthStencilAttachment = &lightingDepthReference;

    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = 1;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkSubpassDescription, 2> subpasses = {geometrySubpass, lightingSubpass};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(vulkanDevice->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) 
    {
        throw std::runtime_error("[致命错误] 延迟渲染双子通道 RenderPass 创建失败！");
    }
}

void FirstApp::createCommandPool() 
{
    QueueFamilyIndices queueFamilyIndices = vulkanDevice->findPhysicalQueueFamilies();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create command pool!");
    }
}

void FirstApp::createCommandBuffers() 
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(vulkanDevice->getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void FirstApp::drawFrame(VulkanCamera& camera, float dt)
{
    vkWaitForFences(vulkanDevice->getDevice(), 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(vulkanDevice->getDevice(), 1, &inFlightFence);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(vulkanDevice->getDevice(), vulkanSwapchain->getSwapchain(), UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(commandBuffer, 0);
    recordCommandBuffer(imageIndex, camera, dt);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {vulkanSwapchain->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(vulkanDevice->getPresentQueue(), &presentInfo);
}

void FirstApp::recordCommandBuffer(uint32_t imageIndex, VulkanCamera& camera, float dt) 
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getSwapChainExtent();
    renderPassInfo.framebuffer = framebuffers[imageIndex];

    std::array<VkClearValue, 6> clearValues{};
    clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}}; 
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};    
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};    
    clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};    
    clearValues[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};    
    clearValues[5].depthStencil = {1.0f, 0}; 

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    GlobalUbo ubo{};
    ubo.projectionView = camera.getProjection() * camera.getView();
    ubo.cameraPos = glm::vec4(cameraObject.transform.translation, 1.0f);

    {
        float time = static_cast<float>(glfwGetTime()); 
        
        ubo.numLights = MAX_POINT_LIGHTS;

        for (int i = 0; i < ubo.numLights; i++) 
        {
            float angle = i * glm::two_pi<float>() /100.0f + time/5.0f;
            float radius =5.0;
            float height =-10+(i % 10)*2.0f;
            ubo.pointLights[i].position = glm::vec4(
                cos(angle) * radius, 
                height, 
                sin(angle) * radius, 
                15.0f 
            );

            float colorAngle = (i/100.0f)*3.14159f; // 颜色变化更慢一些
            ubo.pointLights[i].color = glm::vec4(
                glm::abs(sin(colorAngle * 2.0f)), 
                glm::abs(cos(colorAngle * 0.5f)), 
                1.0f - glm::abs(sin(colorAngle)), 
                50.0f
            );
        }
    }

    memcpy(uboMapped, &ubo, sizeof(ubo));
    static auto startTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - startTime).count();

    if (!gameObjects.empty()) 
    {
        gameObjects[0].transform.rotation.y = time * glm::radians(45.0f);
    }

    FrameInfo frameInfo{
        imageIndex,
        dt,           
        commandBuffer,
        globalDescriptorSet,
        gameObjects
    };

    geometrySystem->render(frameInfo);
    vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

    lightingSystem->render(frameInfo); 
    lightingSystem->renderDebugLights(frameInfo, quadSphereModel);
    imguiSystem->render(commandBuffer, cameraObject, dt);

    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);
}

void FirstApp::createSyncObjects() 
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(vulkanDevice->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(vulkanDevice->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(vulkanDevice->getDevice(), &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create sync objects!");
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

    for (uint32_t i = 6; i <= 9; i++) 
    {
        VkDescriptorSetLayoutBinding inputBinding{};
        inputBinding.binding = i;
        inputBinding.descriptorCount = 1;
        inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; // 关键类型！
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

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[2].descriptorCount = 4;

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

    VkDescriptorImageInfo albedoInfo{albedoTex.sampler, albedoTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normalInfo{normalTex.sampler, normalTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo metallicInfo{metallicTex.sampler, metallicTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo roughnessInfo{roughnessTex.sampler, roughnessTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo environmentInfo{environmentTex.sampler, environmentTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkDescriptorImageInfo posInputInfo{VK_NULL_HANDLE, gBuffer.position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normalInputInfo{VK_NULL_HANDLE, gBuffer.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo albedoInputInfo{VK_NULL_HANDLE, gBuffer.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo pbrInputInfo{VK_NULL_HANDLE, gBuffer.pbr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 10> descriptorWrites{};

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
        write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; // 关键！
        write.descriptorCount = 1;
        write.pImageInfo = imgInfo;
        return write;
    };

    descriptorWrites[6] = writeInput(6, &posInputInfo);
    descriptorWrites[7] = writeInput(7, &normalInputInfo);
    descriptorWrites[8] = writeInput(8, &albedoInputInfo);
    descriptorWrites[9] = writeInput(9, &pbrInputInfo);

    vkUpdateDescriptorSets(vulkanDevice->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void FirstApp::createSingleTexture(const std::string& filepath, TextureData& outTexture, VkFormat format) 
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) 
    {
        throw std::runtime_error("[致命错误] 纹理加载失败！路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vulkanDevice->getDevice(), &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->getDevice(), stagingBuffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &stagingBufferMemory);
    vkBindBufferMemory(vulkanDevice->getDevice(), stagingBuffer, stagingBufferMemory, 0);

    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);
    stbi_image_free(pixels); 

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(vulkanDevice->getDevice(), &imageInfo, nullptr, &outTexture.image);

    vkGetImageMemoryRequirements(vulkanDevice->getDevice(), outTexture.image, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &outTexture.memory);
    vkBindImageMemory(vulkanDevice->getDevice(), outTexture.image, outTexture.memory, 0);

    VkCommandBufferAllocateInfo allocInfoCmd{};
    allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCmd.commandPool = commandPool;
    allocInfoCmd.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkanDevice->getDevice(), &allocInfoCmd, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outTexture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, outTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->getGraphicsQueue());
    vkFreeCommandBuffers(vulkanDevice->getDevice(), commandPool, 1, &commandBuffer);

    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(vulkanDevice->getDevice(), &viewInfo, nullptr, &outTexture.view);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(vulkanDevice->getDevice(), &samplerInfo, nullptr, &outTexture.sampler);
}

void FirstApp::loadAllPBRTextures() 
{
    std::string basePath = "../../Resources/Texture/"; // ⚠️ 请核对你的绝对路径！
    createSingleTexture(basePath + "RuslLessRL/albedo.jpg", albedoTex, VK_FORMAT_R8G8B8A8_UNORM);
    createSingleTexture(basePath + "RuslLessRL/normal.jpg", normalTex, VK_FORMAT_R8G8B8A8_UNORM);
    createSingleTexture(basePath + "RuslLessRL/metallic.jpg", metallicTex, VK_FORMAT_R8G8B8A8_UNORM);
    createSingleTexture(basePath + "RuslLessRL/roughness.jpg", roughnessTex, VK_FORMAT_R8G8B8A8_UNORM);

    std::cout << "[Sentinel 通报] 4 发 PBR 贴图已全部成功轰入 GPU 核心金库！\n";
    std::cout << "[Sentinel 通报] 正在装填 HDR 环境辐射图...\n";
    createHDRTexture(basePath + "environment.hdr", environmentTex);
}

void FirstApp::createHDRTexture(const std::string& filepath, TextureData& outTexture) 
{
    int texWidth, texHeight, texChannels;
    float* pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4 * sizeof(float); 

    if (!pixels) 
    {
        throw std::runtime_error("[致命错误] HDR 全景图加载失败！\n路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));
    }

    std::cout << "[Sentinel 通报] 成功萃取 HDR 浮点光能！大小: " << imageSize / 1024 / 1024 << " MB\n";

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize; // 使用巨大的 imageSize
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vulkanDevice->getDevice(), &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->getDevice(), stagingBuffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &stagingBufferMemory);
    vkBindBufferMemory(vulkanDevice->getDevice(), stagingBuffer, stagingBufferMemory, 0);
    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);
    stbi_image_free(pixels); 

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT; 
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(vulkanDevice->getDevice(), &imageInfo, nullptr, &outTexture.image);

    vkGetImageMemoryRequirements(vulkanDevice->getDevice(), outTexture.image, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &outTexture.memory);
    vkBindImageMemory(vulkanDevice->getDevice(), outTexture.image, outTexture.memory, 0);

    VkCommandBufferAllocateInfo allocInfoCmd{};
    allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCmd.commandPool = commandPool;
    allocInfoCmd.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkanDevice->getDevice(), &allocInfoCmd, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outTexture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, outTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->getGraphicsQueue());
    vkFreeCommandBuffers(vulkanDevice->getDevice(), commandPool, 1, &commandBuffer);

    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(vulkanDevice->getDevice(), &viewInfo, nullptr, &outTexture.view);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; 
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(vulkanDevice->getDevice(), &samplerInfo, nullptr, &outTexture.sampler);
}

void FirstApp::loadGameObjects() 
{
    quadSphereModel = VulkanModel::createModelFromFile(*vulkanDevice, "../../Resources/Models/debug_cube.obj");
    std::shared_ptr<VulkanModel> teapotModel = VulkanModel::createModelFromFile(*vulkanDevice, "../../Resources/Models/Quad-Sphere.obj");
    
    VulkanGameObject teapot = VulkanGameObject::createGameObject();
    teapot.model = teapotModel; // 挂载模型组件
    teapot.transform.rotation = {0.0f, 0.0f, 0.0f}; // 挂载旋转组件，顺时针旋转 180 度，面向正 Z 轴
    teapot.transform.translation = {0.0f, 0.0f, 0.0f}; // 挂载位置组件
    teapot.transform.scale = {1.0f, 1.0f, 1.0f};       // 挂载缩放组件
    
    gameObjects.push_back(std::move(teapot)); // 扔进世界


    std::shared_ptr<VulkanModel> planeModel = VulkanModel::createModelFromFile(*vulkanDevice, "../../Resources/Models/plane.obj");
    
    VulkanGameObject grid = VulkanGameObject::createGameObject();
    grid.model = planeModel;
    grid.isGrid = true;
    grid.transform.translation = {0.0f, -100.0f, 0.0f}; // 稍微往下放一点，垫在茶壶下面
    grid.transform.scale = {1000.0f, 1000.0f, 1000.0f};    // 铺开 100 倍
    
    gameObjects.push_back(std::move(grid));

    std::shared_ptr<VulkanModel> sphereModel = VulkanModel::createModelFromFile(*vulkanDevice, "../../Resources/Models/sphere.obj");
    
    VulkanGameObject skybox = VulkanGameObject::createGameObject();
    skybox.model = sphereModel;
    skybox.transform.scale = {0.1f, 0.1f, 0.1f}; // 放大到足够包裹整个场景的尺寸
    skybox.isSkybox = true; // 挂载天空盒特征标签
    
    gameObjects.push_back(std::move(skybox)); // 扔进世界
}

void FirstApp::createAttachment(VkFormat format, VkImageUsageFlags usage, FrameBufferAttachment* attachment) 
{
    attachment->format = format;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = gBuffer.width;
    imageInfo.extent.height = gBuffer.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vulkanDevice->getDevice(), &imageInfo, nullptr, &attachment->image) != VK_SUCCESS) 
    {
        throw std::runtime_error("[致命错误] G-Buffer 附件 VkImage 创建失败！");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vulkanDevice->getDevice(), attachment->image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &attachment->memory) != VK_SUCCESS) 
    {
        throw std::runtime_error("[致命错误] G-Buffer 附件显存分配失败！");
    }
    vkBindImageMemory(vulkanDevice->getDevice(), attachment->image, attachment->memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.image = attachment->image;

    if (vkCreateImageView(vulkanDevice->getDevice(), &viewInfo, nullptr, &attachment->view) != VK_SUCCESS) 
    {
        throw std::runtime_error("[致命错误] G-Buffer ImageView 创建失败！");
    }
}

void FirstApp::createGBufferResources() 
{
    gBuffer.width = vulkanSwapchain->getSwapChainExtent().width;
    gBuffer.height = vulkanSwapchain->getSwapChainExtent().height;

    std::cout << "[Sentinel 通报] 开始在显存中开辟 G-Buffer 防区...\n";

    createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBuffer.position);
    createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBuffer.normal);
    createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBuffer.albedo);
    createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBuffer.pbr);
    createAttachment(vulkanDevice->findDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &gBuffer.depth);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; 
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    
    if (vkCreateSampler(vulkanDevice->getDevice(), &samplerInfo, nullptr, &gBuffer.sampler) != VK_SUCCESS) 
    {
        throw std::runtime_error("[致命错误] G-Buffer 采样器创建失败！");
    }

    std::cout << "[Sentinel 通报] G-Buffer 锻造完成！5 张底片已就绪。\n";
}

void FirstApp::createFramebuffers() 
{
    framebuffers.resize(vulkanSwapchain->imageCount());
    for (size_t i = 0; i < vulkanSwapchain->imageCount(); i++) 
    {
        std::array<VkImageView, 6> attachments = 
        {
            vulkanSwapchain->getImageView(i), // 0: Swapchain
            gBuffer.position.view,            // 1: Position
            gBuffer.normal.view,              // 2: Normal
            gBuffer.albedo.view,              // 3: Albedo
            gBuffer.pbr.view,                 // 4: PBR
            gBuffer.depth.view                // 5: Depth
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = vulkanSwapchain->getSwapChainExtent().width;
        framebufferInfo.height = vulkanSwapchain->getSwapChainExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(vulkanDevice->getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("[致命错误] G-Buffer 帧缓冲创建失败！");
        }
    }
}