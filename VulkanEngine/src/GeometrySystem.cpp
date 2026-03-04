#include "GeometrySystem.hpp"
#include <stdexcept>

GeometrySystem::GeometrySystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : vulkanDevice(device) {
    createPipelineLayout(globalSetLayout);
    createPipelines(renderPass);
}

GeometrySystem::~GeometrySystem() {
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void GeometrySystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Geometry System: failed to create pipeline layout!");
    }
}

void GeometrySystem::createPipelines(VkRenderPass renderPass) {
    PipelineConfigInfo config{};
    // 这里使用临时的高宽，更专业的做法是在外部处理 Resize，为了演示先借用默认参数
    VulkanPipeline::defaultPipelineConfigInfo(config, 800, 600); 
    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;
    config.subpass = 0;

    // 4 个 MRT 附件配置
    VkPipelineColorBlendAttachmentState defaultAttachment = config.colorBlendAttachments[0];
    config.colorBlendAttachments.assign(4, defaultAttachment);
    config.colorBlendInfo.attachmentCount = 4;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

    // 1. 茶壶管线
    geometryPipeline = std::make_unique<VulkanPipeline>(
        vulkanDevice, "../shaders/mrt.vert.spv", "../shaders/mrt.frag.spv", config);

    // 2. 天空盒管线
    config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT; 
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyboxPipeline = std::make_unique<VulkanPipeline>(
        vulkanDevice, "../shaders/skybox_mrt.vert.spv", "../shaders/skybox_mrt.frag.spv", config);
}

void GeometrySystem::render(FrameInfo& frameInfo) {
    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);

    // 【ECS 系统核心机制】：遍历场景中的所有实体
    for (auto& obj : frameInfo.gameObjects) {
        // 如果这个实体没有挂载模型组件，直接跳过 (比如它可能只是一个空摄像机，或者空气墙)
        if (obj.model == nullptr) continue;

        // 根据实体的组件特征 (Tag)，决定把它喂给哪条流水线
        if (obj.isSkybox) 
        {
            skyboxPipeline->bind(frameInfo.commandBuffer);
            obj.model->bind(frameInfo.commandBuffer);
            obj.model->draw(frameInfo.commandBuffer);
        } 
        else 
        {
            geometryPipeline->bind(frameInfo.commandBuffer);
            obj.model->bind(frameInfo.commandBuffer);

            // 提取组件里的矩阵
            SimplePushConstantData push{};
            push.modelMatrix = obj.transform.mat4();
            push.normalMatrix = obj.transform.mat4();

            vkCmdPushConstants(
                frameInfo.commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->draw(frameInfo.commandBuffer);
        }
    }
}