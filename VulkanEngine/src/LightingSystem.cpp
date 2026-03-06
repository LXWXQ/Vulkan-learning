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

    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);
    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}

void LightingSystem::createDebugLightPipeline(VkRenderPass renderPass) 
{
    PipelineConfigInfo config{};
    VulkanPipeline::defaultPipelineConfigInfo(config, 1920, 1080);
    
    config.renderPass = renderPass;
    config.subpass = 1;
    config.pipelineLayout = pipelineLayout;

    auto bindingDescriptions = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    config.bindingDescriptions = {bindingDescriptions};
    config.attributeDescriptions = attributeDescriptions;

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    
    config.colorBlendAttachments[0].blendEnable = VK_TRUE;
    config.colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    config.colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    config.colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    
    config.colorBlendInfo.attachmentCount = 1;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

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

    vkCmdDrawIndexed(
        frameInfo.commandBuffer, 
        sphereModel->getIndexCount(), 
        100, 
        0, 0, 0); 
}