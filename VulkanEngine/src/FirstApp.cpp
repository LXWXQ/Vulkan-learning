#include "FirstApp.hpp"
#include <stdexcept>
#include <array>

// 构造函数：精准控制引擎点火顺序
FirstApp::FirstApp() {
    initWindow();
    // 1. 建厂房
    vulkanDevice = std::make_unique<VulkanDevice>(window);
    loadGameObjects();
    // 2. 建传送带
    VkExtent2D extent{WIDTH, HEIGHT};
    vulkanSwapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, extent);
    
    // 3. 制定工序单，并为传送带挂上相框
    createRenderPass();
    vulkanSwapchain->createFramebuffers(renderPass); 
    
    // 4. 建机床 (管线)
    createPipelineLayout();
    createPipeline();
    
    // 5. 组建指挥部与红绿灯
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

FirstApp::~FirstApp() {
    vkDestroySemaphore(vulkanDevice->getDevice(), renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(vulkanDevice->getDevice(), imageAvailableSemaphore, nullptr);
    vkDestroyFence(vulkanDevice->getDevice(), inFlightFence, nullptr);
    
    vkDestroyCommandPool(vulkanDevice->getDevice(), commandPool, nullptr);
    vkDestroyPipelineLayout(vulkanDevice->getDevice(), pipelineLayout, nullptr);
    vkDestroyRenderPass(vulkanDevice->getDevice(), renderPass, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();
}

void FirstApp::run() {
    while (!glfwWindowShouldClose(window)) {
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

void FirstApp::createRenderPass() {
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

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(vulkanDevice->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void FirstApp::createPipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // 顶点和片段着色器都能读
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void FirstApp::createPipeline() {
    PipelineConfigInfo pipelineConfig{};
    VulkanPipeline::defaultPipelineConfigInfo(pipelineConfig, vulkanSwapchain->getSwapChainExtent().width, vulkanSwapchain->getSwapChainExtent().height);
    
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;

    // 哨兵提醒：注意这里的路径！确保 shaders 文件夹在执行目录的上一级
    vulkanPipeline = std::make_unique<VulkanPipeline>(
        *vulkanDevice,
        "../shaders/shader.vert.spv",
        "../shaders/shader.frag.spv",
        pipelineConfig
    );
}

void FirstApp::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = vulkanDevice->findPhysicalQueueFamilies();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void FirstApp::createCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(vulkanDevice->getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void FirstApp::recordCommandBuffer(uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    // 动态获取当前应该画哪张画板
    renderPassInfo.framebuffer = vulkanSwapchain->getFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getSwapChainExtent();

    VkClearValue clearColor = {{{0.01f, 0.01f, 0.01f, 1.0f}}}; // 深灰色背景
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vulkanPipeline->bind(commandBuffer);
    vulkanModel->bind(commandBuffer);

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    SimplePushConstantData push{};
    push.offset = {0.0f, -0.2f + 0.2f * glm::sin(time * 3.0f)}; // 让它上下弹跳
    push.color = {0.8f, 0.2f, 0.2f}; // 染成红色

    // 生成旋转矩阵
    float s = glm::sin(time);
    float c = glm::cos(time);
    push.transform = glm::mat2{c, s, -s, c};

    // 将数据压入指令流！
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

void FirstApp::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(vulkanDevice->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(vulkanDevice->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(vulkanDevice->getDevice(), &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sync objects!");
    }
}

void FirstApp::drawFrame() {
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

    if (vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
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

void FirstApp::loadGameObjects() {
    // 终于！三角形的坐标回到了 C++ 的控制之下！
    std::vector<Vertex> vertices{
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // 顶：红
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},  // 右：绿
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}  // 左：蓝
    };

    vulkanModel = std::make_unique<VulkanModel>(*vulkanDevice, vertices);
}