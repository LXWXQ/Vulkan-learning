#include "Renderer.h"
#include <stdexcept>
#include <array>
#include <iostream>

Renderer::Renderer(Device& device, Swapchain& swapchain)
    : vulkanDevice(device), vulkanSwapchain(swapchain)
{
    createCommandPools();
    createCommandBuffers();
    createSyncObjects();

    std::cout << "[Renderer] D-4/D-5 active: " << kFramesInFlight
              << " frames in flight, graphics + compute timelines, "
              << (vulkanDevice.hasDedicatedComputeQueue() ? "dedicated async compute" : "compute fallback on graphics queue")
              << "\n";
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    for (VkSemaphore semaphore : imageAvailableSemaphores)
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(vulkanDevice.getDevice(), semaphore, nullptr);

    for (VkSemaphore semaphore : renderFinishedSemaphores)
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(vulkanDevice.getDevice(), semaphore, nullptr);

    if (graphicsTimelineSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(vulkanDevice.getDevice(), graphicsTimelineSemaphore, nullptr);
    if (computeTimelineSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(vulkanDevice.getDevice(), computeTimelineSemaphore, nullptr);

    if (computeCommandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(vulkanDevice.getDevice(), computeCommandPool, nullptr);
    vkDestroyCommandPool(vulkanDevice.getDevice(), commandPool, nullptr);
}

void Renderer::createCommandPools()
{
    QueueFamilyIndices queueFamilyIndices = vulkanDevice.findPhysicalQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    if (vkCreateCommandPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics command pool!");

    // 即使 compute fallback 到同一个 family，也保留独立 pool：D-5 以后需要
    // 给 compute 命令单独重置/回收时，不会和 graphics 命令相互影响。
    poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();
    if (vkCreateCommandPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &computeCommandPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create compute command pool!");
}

void Renderer::createCommandBuffers()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;

    allocInfo.commandPool = commandPool;
    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate per-frame graphics command buffers!");

    allocInfo.commandPool = computeCommandPool;
    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate per-frame compute command buffers!");
}

void Renderer::createSyncObjects()
{
    VkSemaphoreCreateInfo binaryInfo{};
    binaryInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (VkSemaphore& semaphore : imageAvailableSemaphores)
    {
        if (vkCreateSemaphore(vulkanDevice.getDevice(), &binaryInfo, nullptr, &semaphore) != VK_SUCCESS)
            throw std::runtime_error("failed to create image-available semaphore!");
    }

    renderFinishedSemaphores.resize(vulkanSwapchain.imageCount(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : renderFinishedSemaphores)
    {
        if (vkCreateSemaphore(vulkanDevice.getDevice(), &binaryInfo, nullptr, &semaphore) != VK_SUCCESS)
            throw std::runtime_error("failed to create render-finished semaphore!");
    }

    VkSemaphoreTypeCreateInfo timelineType{};
    timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue = 0;

    VkSemaphoreCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    timelineInfo.pNext = &timelineType;

    if (vkCreateSemaphore(vulkanDevice.getDevice(), &timelineInfo, nullptr, &graphicsTimelineSemaphore) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics timeline semaphore!");
    if (vkCreateSemaphore(vulkanDevice.getDevice(), &timelineInfo, nullptr, &computeTimelineSemaphore) != VK_SUCCESS)
        throw std::runtime_error("failed to create compute timeline semaphore!");
}

void Renderer::waitForSemaphoreValue(VkSemaphore semaphore, uint64_t value) const
{
    if (value == 0)
        return;

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
    waitInfo.pValues = &value;

    if (vkWaitSemaphores(vulkanDevice.getDevice(), &waitInfo, UINT64_MAX) != VK_SUCCESS)
        throw std::runtime_error("failed to wait for timeline semaphore!");
}

void Renderer::waitForFrameSlot(uint32_t frameInFlightIndex)
{
    // 两条队列都可能访问这个 slot 的资源，所以两个 timeline 都必须完成。
    waitForSemaphoreValue(graphicsTimelineSemaphore, graphicsSubmissionValues[frameInFlightIndex]);
    waitForSemaphoreValue(computeTimelineSemaphore, computeSubmissionValues[frameInFlightIndex]);
}

VkCommandBuffer Renderer::beginFrame()
{
    currentFrameInFlightIndex = static_cast<uint32_t>(frameCounter % kFramesInFlight);
    waitForFrameSlot(currentFrameInFlightIndex);

    VkSemaphore acquireSemaphore = imageAvailableSemaphores[currentFrameInFlightIndex];
    const VkResult acquireResult = vkAcquireNextImageKHR(
        vulkanDevice.getDevice(), vulkanSwapchain.getSwapchain(), UINT64_MAX,
        acquireSemaphore, VK_NULL_HANDLE, &currentImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        throw std::runtime_error("swapchain out of date; window resize is not implemented yet");
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("failed to acquire swapchain image!");

    VkCommandBuffer graphicsCommandBuffer = commandBuffers[currentFrameInFlightIndex];
    VkCommandBuffer computeCommandBuffer = computeCommandBuffers[currentFrameInFlightIndex];

    if (vkResetCommandBuffer(graphicsCommandBuffer, 0) != VK_SUCCESS ||
        vkResetCommandBuffer(computeCommandBuffer, 0) != VK_SUCCESS)
        throw std::runtime_error("failed to reset command buffers!");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(graphicsCommandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin graphics command buffer!");
    if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin compute command buffer!");

    ++frameCounter;   // 当前帧的 1-based 逻辑帧号，也作为两条 timeline 的 value
    return graphicsCommandBuffer;
}

void Renderer::endFrame()
{
    VkCommandBuffer graphicsCommandBuffer = commandBuffers[currentFrameInFlightIndex];
    VkCommandBuffer computeCommandBuffer = computeCommandBuffers[currentFrameInFlightIndex];
    const uint64_t signalValue = frameCounter;

    if (vkEndCommandBuffer(graphicsCommandBuffer) != VK_SUCCESS ||
        vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to end command buffers!");

    // ---- 1) compute queue：独立 submit + 独立 timeline ----
    // 当前 D-5 先支持「与 graphics 无资源依赖」的异步计算任务；RenderGraph 会在
    // 检测到跨队列读写时抛错，避免这条 submit 变成静默数据竞争。
    {
        VkTimelineSemaphoreSubmitInfo timelineSubmit{};
        timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineSubmit.signalSemaphoreValueCount = 1;
        timelineSubmit.pSignalSemaphoreValues = &signalValue;

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineSubmit;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &computeCommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &computeTimelineSemaphore;

        if (vkQueueSubmit(vulkanDevice.getComputeQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("failed to submit compute command buffer!");

        computeSubmissionValues[currentFrameInFlightIndex] = signalValue;
    }

    // ---- 2) graphics queue：原来的 frame 提交 + present ----
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrameInFlightIndex] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = {
        renderFinishedSemaphores[currentImageIndex],
        graphicsTimelineSemaphore
    };

    uint64_t waitValues[] = { 0 };
    uint64_t signalValues[] = { 0, signalValue };

    VkTimelineSemaphoreSubmitInfo timelineSubmit{};
    timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineSubmit.waitSemaphoreValueCount = 1;
    timelineSubmit.pWaitSemaphoreValues = waitValues;
    timelineSubmit.signalSemaphoreValueCount = 2;
    timelineSubmit.pSignalSemaphoreValues = signalValues;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineSubmit;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &graphicsCommandBuffer;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer!");

    graphicsSubmissionValues[currentFrameInFlightIndex] = signalValue;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphores[0];
    VkSwapchainKHR swapChains[] = { vulkanSwapchain.getSwapchain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &currentImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(vulkanDevice.getPresentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
        throw std::runtime_error("swapchain out of date during present; window resize is not implemented yet");
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("failed to present swapchain image!");
}

#if 0   // D-1(b): 已改用 RenderGraph + 动态渲染，以下接口不再需要
void Renderer::beginRenderPass(VkCommandBuffer commandBuffer, VkRenderPass renderPass, VkFramebuffer framebuffer, const std::vector<VkClearValue>& clearValues) 
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain.getSwapChainExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Renderer::endRenderPass(VkCommandBuffer commandBuffer) 
{
    vkCmdEndRenderPass(commandBuffer);
}
#endif
