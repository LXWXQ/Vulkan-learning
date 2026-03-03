#include "FirstApp.hpp"
#include <stdexcept>
#include <array>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// 构造函数：控制引擎点火顺序
FirstApp::FirstApp() 
{
    initWindow();
    // 1. 建厂
    vulkanDevice = std::make_unique<VulkanDevice>(window);
    loadGameObjects();
    createCommandPool();
    // 2. 建传送带
    VkExtent2D extent{WIDTH, HEIGHT};
    vulkanSwapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, extent);
    
    // 3. 制定工序单，并为传送带挂上工序单
    createRenderPass();
    vulkanSwapchain->createFramebuffers(renderPass); 
    
    // 4. 组建生产线 (Pipeline)，注意它依赖工序单和传送带的分辨率
    createDescriptorSetLayout();
    loadAllPBRTextures(); 
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createPipelineLayout();
    createPipeline();
    
    // 5. 组建指挥部与红绿灯 (命令池和同步对象)
    
    createCommandBuffers();
    createSyncObjects();
}

FirstApp::~FirstApp() 
{
    auto destroyTexture = [&](TextureData& tex) 
    {
        vkDestroySampler(vulkanDevice->getDevice(), tex.sampler, nullptr);
        vkDestroyImageView(vulkanDevice->getDevice(), tex.view, nullptr);
        vkDestroyImage(vulkanDevice->getDevice(), tex.image, nullptr);
        vkFreeMemory(vulkanDevice->getDevice(), tex.memory, nullptr);
    };

    // PBR 阵列
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
    vkDestroyPipelineLayout(vulkanDevice->getDevice(), pipelineLayout, nullptr);
    vkDestroyRenderPass(vulkanDevice->getDevice(), renderPass, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();
}

void FirstApp::run() 
{
    while (!glfwWindowShouldClose(window)) 
    {
        glfwPollEvents();
        drawFrame();
    }
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void FirstApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Engine - Sentinel", nullptr, nullptr);
}

void FirstApp::createRenderPass() 
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = vulkanSwapchain->getSwapChainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = vulkanDevice->findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1; 
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(vulkanDevice->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create render pass!");
    }
}

void FirstApp::createPipelineLayout() 
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // 顶点和片段着色器都能�?
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void FirstApp::createPipeline() {
    PipelineConfigInfo pipelineConfig{};
    VulkanPipeline::defaultPipelineConfigInfo(pipelineConfig, vulkanSwapchain->getSwapChainExtent().width, vulkanSwapchain->getSwapChainExtent().height);
    
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;

    // 哨兵提醒：注意这里的�?径！�?�? shaders 文件夹在执�?�目录的上一�?
    vulkanPipeline = std::make_unique<VulkanPipeline>(
        *vulkanDevice,
        "../shaders/shader.vert.spv",
        "../shaders/shader.frag.spv",
        pipelineConfig
    );
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

void FirstApp::recordCommandBuffer(uint32_t imageIndex) 
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    // 动态获取当前应该画�?张画�?
    renderPassInfo.framebuffer = vulkanSwapchain->getFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getSwapChainExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vulkanPipeline->bind(commandBuffer);
    vulkanModel->bind(commandBuffer);

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    glm::mat4 model = glm::mat4(1.0f);
    
    model = glm::rotate(model, time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    
    model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.1f));
    
    model = glm::translate(model, glm::vec3(0.0f, 0.0F, 0.0f)); 

    glm::mat4 view = glm::lookAt(glm::vec3(500.0f, 500.0f, 500.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)WIDTH / (float)HEIGHT, 0.1f, 1000.0f);
    proj[1][1] *= -1; 

    
    GlobalUbo ubo{};
    ubo.projectionView = proj * view;
    ubo.cameraPos = glm::vec4(500.0f, 500.0f, 500.0f, 1.0f);
    // 光照方向和颜色我们在结构体里已经给了极佳的默认值，直接使用即可
    memcpy(uboMapped, &ubo, sizeof(ubo));
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, // 第一个 set (对应 layout(set = 0))
        1,
        &globalDescriptorSet,
        0,
        nullptr);

    SimplePushConstantData push{};
    push.modelMatrix = model;
    push.normalMatrix = model;
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(SimplePushConstantData),
        &push);
        

    vulkanModel->draw(commandBuffer);
    //vkCmdDraw(commandBuffer, 3, 1, 0, 0);
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

void FirstApp::drawFrame()
{
    vkWaitForFences(vulkanDevice->getDevice(), 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(vulkanDevice->getDevice(), 1, &inFlightFence);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(vulkanDevice->getDevice(), vulkanSwapchain->getSwapchain(), UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(commandBuffer, 0);
    recordCommandBuffer(imageIndex);

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

void FirstApp::createDescriptorSetLayout()
 {
    // 描述我们要绑定的 UBO
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0; // 对应 Shader 里的 binding = 0
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    // 只有顶点和片段着色器需要读取全局光照和矩阵
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

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice->getDevice(), &layoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("无法创建 PBR 描述符集布局！");
    }
}

void FirstApp::createUniformBuffers() 
{
    VkDeviceSize bufferSize = sizeof(GlobalUbo);

    // 1. 创建逻辑 Buffer (用途：UNIFORM_BUFFER)
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice->getDevice(), &bufferInfo, nullptr, &globalUboBuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create uniform buffer!");
    }

    // 2. 申请物理显存 (要求 CPU 可见，且内存自动同步)
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

    // 3. 架构师性能秘诀：永久映射 (Persistent Mapping)
    // 我们不每次渲染都去 map/unmap，而是打开显存盖子后就不关了，每帧直接往里倒数据！
    vkMapMemory(vulkanDevice->getDevice(), globalUboBufferMemory, 0, bufferSize, 0, &uboMapped);
}

void FirstApp::createDescriptorPool() 
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    // UBO 依然只需要 1 个
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    
    // 【核心扩容】：告诉内存池，我们要放 4 个图像采样器进去！
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5; // 4 张 PBR 贴图 + 1 张环境贴图

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1; // 依然只分配一个整体的 Set

    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("无法创建 PBR 描述符池！");
    }
}

void FirstApp::createDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &globalSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &allocInfo, &globalDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("无法分配 PBR 描述符集！");
    }

    // --- 准备数据指针 ---
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = globalUboBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GlobalUbo);

    // 提取 4 张贴图的显存指针信息
    VkDescriptorImageInfo albedoInfo{albedoTex.sampler, albedoTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normalInfo{normalTex.sampler, normalTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo metallicInfo{metallicTex.sampler, metallicTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo roughnessInfo{roughnessTex.sampler, roughnessTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo environmentInfo{environmentTex.sampler, environmentTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // --- 组装 5 条写入指令 ---
    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
    
    // 0 号：UBO
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = globalDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    // 使用一个 Lambda 表达式来简化重复的图片写入代码
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
    vkUpdateDescriptorSets(vulkanDevice->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void FirstApp::createSingleTexture(const std::string& filepath, TextureData& outTexture, VkFormat format) 
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("[致命错误] 纹理加载失败！路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));
    }

    // --- 1. 建造中转站 ---
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    // (此处代码和你昨天写的一模一样)
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

    // --- 2. CPU 倒灌 ---
    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);
    stbi_image_free(pixels); 

    // --- 3. 建造金库 ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format; // 【核心改变】：接收外部传入的格式！(SRGB 或 UNORM)
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

    // --- 4. 屏障与搬运 ---
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

    // --- 5. 观察镜与采样器 ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format; // 【同步格式】
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
    
    std::cout << "[Sentinel 通报] 正在装填 PBR 弹药库...\n";

    // 只有 Albedo 是 sRGB 视觉颜色
    createSingleTexture(basePath + "albedo.png", albedoTex, VK_FORMAT_R8G8B8A8_SRGB);
    // 其余全部是 UNORM 物理数据！
    createSingleTexture(basePath + "normal.jpg", normalTex, VK_FORMAT_R8G8B8A8_UNORM);
    createSingleTexture(basePath + "metallic.jpg", metallicTex, VK_FORMAT_R8G8B8A8_UNORM);
    createSingleTexture(basePath + "roughness.jpg", roughnessTex, VK_FORMAT_R8G8B8A8_UNORM);

    std::cout << "[Sentinel 通报] 4 发 PBR 贴图已全部成功轰入 GPU 核心金库！\n";
    std::cout << "[Sentinel 通报] 正在装填 HDR 环境辐射图...\n";
    createHDRTexture(basePath + "environment.hdr", environmentTex);
}

void FirstApp::createHDRTexture(const std::string& filepath, TextureData& outTexture) 
{
    int texWidth, texHeight, texChannels;
    
    // 【核心黑科技】：使用 stbi_loadf (带 f) 来读取绝对的物理浮点数光能！
    // 它返回的不再是 stbi_uc* (8位整数)，而是 float* (32位浮点数)
    float* pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    // 【容量翻倍】：宽 * 高 * 4个通道 * 每个 float 占 4 字节 = 巨大的显存开销！
    VkDeviceSize imageSize = texWidth * texHeight * 4 * sizeof(float); 

    if (!pixels) {
        throw std::runtime_error("[致命错误] HDR 全景图加载失败！\n路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));
    }

    std::cout << "[Sentinel 通报] 成功萃取 HDR 浮点光能！大小: " << imageSize / 1024 / 1024 << " MB\n";

    // --- 1. 建造中转站 ---
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

    // --- 2. CPU 倒灌 (由于是指针，memcpy 的用法和以前一模一样) ---
    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);
    
    // 释放 float* 指针占用的 CPU 内存
    stbi_image_free(pixels); 

    // --- 3. 建造浮点金库 ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    // 【物理空间宣告】：强制声明为 32 位浮点线性空间！
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

    // --- 4. 屏障与搬运 ---
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

    // --- 5. 观察镜与采样器 ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT; // 【同步浮点格式】
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
    // 天空穹顶的边缘不能有黑边截断
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; 
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(vulkanDevice->getDevice(), &samplerInfo, nullptr, &outTexture.sampler);
}

void FirstApp::loadGameObjects() 
{
    // 6 �?顶点（上下两极，�?间四�?角）
    /*std::vector<Vertex> vertices{
        {{ 0.0f, -0.5f,  0.0f}, {1.0f, 0.0f, 0.0f}}, // 0: 顶部 (�?)
        {{ 0.0f,  0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}}, // 1: 底部 (�?)
        {{-0.5f,  0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 2: 前左 (�?)
        {{ 0.5f,  0.0f,  0.5f}, {1.0f, 1.0f, 0.0f}}, // 3: 前右 (�?)
        {{ 0.5f,  0.0f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 4: 后右 (�?)
        {{-0.5f,  0.0f, -0.5f}, {0.0f, 1.0f, 1.0f}}  // 5: 后左 (�?)
    };

    // 8 �?�?，共 24 �?索引序号
    std::vector<uint32_t> indices{
        0, 2, 3, // 上半�? 4 �?�?
        0, 3, 4,
        0, 4, 5,
        0, 5, 2,
        1, 3, 2, // 下半�? 4 �?�?
        1, 4, 3,
        1, 5, 4,
        1, 2, 5
    };

    // 将顶点和索引一起发给显�?
    vulkanModel = std::make_unique<VulkanModel>(*vulkanDevice, vertices, indices);*/
    vulkanModel = VulkanModel::createModelFromFile(*vulkanDevice, "../../Resources/Models/teapot.obj");
}