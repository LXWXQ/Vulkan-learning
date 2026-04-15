#include "LightingPass.h"
#include <stdexcept>
#include <array>

LightingPass::LightingPass(Device& device, GBuffer& gBuffer, Swapchain& swapchain, VkDescriptorSetLayout globalLayout)
    : vulkanDevice(device), gBuffer(gBuffer), vulkanSwapchain(swapchain), globalSetLayout(globalLayout) 
{
}

LightingPass::~LightingPass() 
{
    for (auto fb : framebuffers) {
        vkDestroyFramebuffer(vulkanDevice.getDevice(), fb, nullptr);
    }
    vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}

void LightingPass::init() 
{
    createRenderPass();
    createFramebuffers();
    
    // 🌟 直接把存下来的 globalSetLayout 传给 System 去建管线
    lightingSystem = std::make_unique<LightingSystem>(vulkanDevice, renderPass, globalSetLayout);
}

void LightingPass::execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) 
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[frameInfo.frameIndex]; 
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain.getSwapChainExtent();

    std::array<VkClearValue, 1> clearValues{};
    clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    VkDescriptorBufferInfo uboInfo{frameInfo.globalUboBuffer, 0, sizeof(GlobalUbo)};
    VkDescriptorSet lightingSet;

    DescriptorBuilder::begin(frameInfo.allocator, frameInfo.globalSetLayout)
        .bindBuffer(0, &uboInfo)
        .bindImage(1, &frameInfo.dummyInfo) // PBR 已经画进 GBuffer，不需要重读了
        .bindImage(2, &frameInfo.dummyInfo)
        .bindImage(3, &frameInfo.dummyInfo)
        .bindImage(4, &frameInfo.dummyInfo)
        .bindImage(5, &frameInfo.environmentInfo) // 🌟 读 IBL 环境光
        .bindImage(6, &frameInfo.posInputInfo)    // 🌟 读 GBuffer 全家桶
        .bindImage(7, &frameInfo.normalInputInfo)
        .bindImage(8, &frameInfo.albedoInputInfo)
        .bindImage(9, &frameInfo.pbrInputInfo)
        .bindImage(10, &frameInfo.ssaoInfo)       // 🌟 读做好的 SSAO
        .build(lightingSet);

    // 绑定新鲜的 Set，并交给 System 去执行 Draw
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingSystem->getPipelineLayout(), 0, 1, &lightingSet, 0, nullptr);

    // 🌟 直接把 frameInfo 传给 System 画光照合成
    lightingSystem->render(frameInfo);
    
    // 画光源小球
    /*if (debugModel != nullptr) 
    {
        lightingSystem->renderDebugLights(frameInfo, debugModel);
    }*/

    // 画 UI
    if (imguiSystem != nullptr) 
    {
        GameObject dummyCamera = GameObject::createGameObject();
        imguiSystem->render(commandBuffer, dummyCamera, frameInfo.frameTime, frameInfo.settings, frameInfo.telemetry);
    }

    vkCmdEndRenderPass(commandBuffer);
}

void LightingPass::createRenderPass() 
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = vulkanSwapchain.getSwapChainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; 
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) 
    {
        throw std::runtime_error("failed to create lighting render pass!");
    }
}

void LightingPass::createFramebuffers() 
{
    framebuffers.resize(vulkanSwapchain.imageCount());
    for (size_t i = 0; i < vulkanSwapchain.imageCount(); i++) 
    {
        VkImageView attachment = vulkanSwapchain.getImageView(i);
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &attachment;
        framebufferInfo.width = vulkanSwapchain.getSwapChainExtent().width;
        framebufferInfo.height = vulkanSwapchain.getSwapChainExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to create lighting framebuffer!");
        }
    }
}