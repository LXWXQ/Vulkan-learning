#include "LightingSystem.hpp"
#include <stdexcept>

LightingSystem::LightingSystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : vulkanDevice(device) {
    createPipelineLayout(globalSetLayout);
    createPipeline(renderPass);
}

LightingSystem::~LightingSystem() {
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void LightingSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
    // 【架构优势】：光照系统根本不需要 Push Constants！所以我们不传，极大节省性能！
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Lighting System: failed to create pipeline layout!");
    }
}

void LightingSystem::createPipeline(VkRenderPass renderPass) {
    PipelineConfigInfo config{};
    VulkanPipeline::defaultPipelineConfigInfo(config, 800, 600);
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
}

void LightingSystem::render(FrameInfo& frameInfo) {
    lightingPipeline->bind(frameInfo.commandBuffer);
    
    // 再次绑定属于 Lighting 自己的 DescriptorSets
    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);

    // 无 VBO 强行绘制大三角形
    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}