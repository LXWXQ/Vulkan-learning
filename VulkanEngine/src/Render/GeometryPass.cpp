// GeometryPass.cpp
#include "GeometryPass.h"
#include <array>
#include <stdexcept>

GeometryPass::GeometryPass(Device& device, GBuffer& gBuffer, VkDescriptorSetLayout setLayout)
    : vulkanDevice(device), gBuffer(gBuffer), globalSetLayout(setLayout) {}

GeometryPass::~GeometryPass() 
{
    vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffer, nullptr);
    vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}

void GeometryPass::init() 
{
    createRenderPass();
    createFramebuffer();
    // 🌟 核心修复：RenderPass 创建好之后，立刻利用它创建 System！
    geometrySystem = std::make_unique<GeometrySystem>(vulkanDevice, renderPass, globalSetLayout);
}
void GeometryPass::createRenderPass() 
{
    // 只有 5 个附件 (Pos, Normal, Albedo, PBR, Depth)
    std::array<VkAttachmentDescription, 5> attachments{};
    for (int i = 0; i < 4; i++) 
    {
        attachments[i].format = (i == 0 || i == 1) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 必须 Store，因为后面的 Pass 要读
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // 渲染完后，自动转换为 Shader 可读模式！
        attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
    }

    attachments[4].format = vulkanDevice.findDepthFormat();
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

   std::array<VkAttachmentReference, 4> colorRefs = {
    VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    VkAttachmentReference{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    VkAttachmentReference{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    VkAttachmentReference{3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
};
    VkAttachmentReference depthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    // 省略依赖设置 (VkSubpassDependency)，和普通 RenderPass 类似
    // ...

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Geometry RenderPass failed!");
    }
}

void GeometryPass::createFramebuffer() 
{
    // 1. 收集 G-Buffer 的 5 张底片
    std::array<VkImageView, 5> attachments = {
        gBuffer.getPosition().view,
        gBuffer.getNormal().view,
        gBuffer.getAlbedo().view,
        gBuffer.getPbr().view,
        gBuffer.getDepth().view
    };

    // 2. 配置 Framebuffer 信息
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass; // 绑定到当前 Pass 的 RenderPass
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    
    framebufferInfo.width = WIDTH; 
    framebufferInfo.height = HEIGHT;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) 
    {
        throw std::runtime_error("致命错误：无法创建 GeometryPass 的 Framebuffer！");
    }
}

void GeometryPass::execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) 
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    
    // 设置渲染区域大小 (注意：如果你的 GBuffer 里没有 getExtent 方法，可以先用 FirstApp 里的全局变量，或去 GBuffer 里加一个)
    renderPassInfo.renderArea.offset = {0, 0};
    
    // 如果你 GBuffer 没暴露尺寸，可以直接硬编码 {1920, 1080} 测试，或者最好去 VulkanGBuffer.hpp 里加个 getExtent() 方法
    // renderPassInfo.renderArea.extent = gBuffer.getExtent(); 
    renderPassInfo.renderArea.extent = {WIDTH, HEIGHT}; 

    // G-Buffer 需要清空 5 张底片 (位置、法线、反照率、PBR、深度)
    std::array<VkClearValue, 5> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // Position
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // Normal
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // Albedo
    clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // PBR
    clearValues[4].depthStencil = {1.0f, 0};           // Depth

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    VkDescriptorBufferInfo uboInfo{frameInfo.globalUboBuffer, 0, sizeof(GlobalUbo)};
    VkDescriptorSet geometrySet;
    
    DescriptorBuilder::begin(frameInfo.allocator, frameInfo.globalSetLayout)
        .bindBuffer(0, &uboInfo)
        .bindImage(1, &frameInfo.albedoInfo)
        .bindImage(2, &frameInfo.normalInfo)
        .bindImage(3, &frameInfo.metallicInfo)
        .bindImage(4, &frameInfo.roughnessInfo)
        .bindImage(5, &frameInfo.environmentInfo) // 几何阶段不读环境光
        .bindImage(6, &frameInfo.dummyInfo) // 正在写 GBuffer，全用替身堵上槽位！
        .bindImage(7, &frameInfo.dummyInfo)
        .bindImage(8, &frameInfo.dummyInfo)
        .bindImage(9, &frameInfo.dummyInfo)
        .bindImage(10, &frameInfo.dummyInfo) // 还没算 SSAO，用替身
        .build(geometrySet);
        
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometrySystem->getPipelineLayout(), 0, 1, &geometrySet, 0, nullptr);
    // 🔥 就是缺了这句最核心的指令！告诉 GPU 开始在这个帧缓冲上画画！
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 开始提交 3D 几何体的绘制指令 (它内部调用的 vkCmdDrawIndexed 现在安全了)
    geometrySystem->render(frameInfo);

    // 画完收工
    vkCmdEndRenderPass(commandBuffer);
}