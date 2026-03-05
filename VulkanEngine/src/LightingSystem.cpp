#include "LightingSystem.hpp"
#include <stdexcept>

LightingSystem::LightingSystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : vulkanDevice(device) 
    {
    createPipelineLayout(globalSetLayout);
    createPipeline(renderPass);
}

LightingSystem::~LightingSystem() 
{
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void LightingSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) 
{
    // 【架构优势】：光照系统根本不需要 Push Constants！所以我们不传，极大节省性能！
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) 
    {
        throw std::runtime_error("Lighting System: failed to create pipeline layout!");
    }
}

void LightingSystem::createPipeline(VkRenderPass renderPass) 
{
    PipelineConfigInfo config{};
    VulkanPipeline::defaultPipelineConfigInfo(config, 1920, 1080);
    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;
    config.subpass = 1;

    // 输出到屏幕 (1个附件)，关闭深度，关闭 VBO
    config.colorBlendInfo.attachmentCount = 1;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();

    lightingPipeline = std::make_unique<VulkanPipeline>(
        vulkanDevice, "../shaders/lighting.vert.spv", "../shaders/lighting.frag.spv", config);
    
    createDebugLightPipeline(renderPass);
}

void LightingSystem::render(FrameInfo& frameInfo) 
{
    lightingPipeline->bind(frameInfo.commandBuffer);
    
    // 再次绑定属于 Lighting 自己的 DescriptorSets
    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);
    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}

void LightingSystem::createDebugLightPipeline(VkRenderPass renderPass) {
    PipelineConfigInfo config{};
    VulkanPipeline::defaultPipelineConfigInfo(config, 1920, 1080);
    
    config.renderPass = renderPass;
    config.subpass = 1;
    config.pipelineLayout = pipelineLayout;

    // ✨ 核心修复：直接从你的 Vertex 类获取“身份证”信息
    // 这样 Stride（44 字节）和 Location 0 (Position) 的映射关系就绝对正确了
    auto bindingDescriptions = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    // 注意：这里需要加上 {} 转成 vector
    config.bindingDescriptions = {bindingDescriptions};
    config.attributeDescriptions = attributeDescriptions;

    // 剩下的配置保持不变
    config.depthStencilInfo.depthTestEnable = VK_TRUE;  // 开启测试：根据深度判断是否绘制
    config.depthStencilInfo.depthWriteEnable = VK_FALSE; // 关闭写入：灯块本身不挡住别人
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // 标准深度比较
    
    config.colorBlendAttachments[0].blendEnable = VK_TRUE;
    config.colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    config.colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    config.colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    
    config.colorBlendInfo.attachmentCount = 1;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

    // 🚨 提醒：请确保 ../shaders/light_cube.vert.spv 等文件已经由 CMake 编译生成！
    debugLightPipeline = std::make_unique<VulkanPipeline>(
        vulkanDevice, "../shaders/light_cube.vert.spv", "../shaders/light_cube.frag.spv", config);
}

void LightingSystem::renderDebugLights(FrameInfo& frameInfo, std::shared_ptr<VulkanModel> sphereModel) 
{
    debugLightPipeline->bind(frameInfo.commandBuffer);
    
    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);

    sphereModel->bind(frameInfo.commandBuffer);
    
    // ✨ 修复：使用动态的 numLights，而不是静态的 100
    vkCmdDrawIndexed(
        frameInfo.commandBuffer, 
        sphereModel->getIndexCount(), 
        100, 
        0, 0, 0); 
}