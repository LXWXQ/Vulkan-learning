#include "GeometrySystem.hpp"
#include <stdexcept>

GeometrySystem::GeometrySystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : vulkanDevice(device) 
{
    createPipelineLayout(globalSetLayout);
    createPipelines(renderPass);
}

GeometrySystem::~GeometrySystem() 
{
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

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) 
    {
        throw std::runtime_error("Geometry System: failed to create pipeline layout!");
    }
}

void GeometrySystem::createPipelines(VkRenderPass renderPass)
 {
    PipelineConfigInfo config{};
    // 这里使用临时的高宽，更专业的做法是在外部处理 Resize，为了演示先借用默认参数
    VulkanPipeline::defaultPipelineConfigInfo(config, 1920, 1080); 
    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;
    config.subpass = 0;

    // 4 个 MRT 附件配置
    VkPipelineColorBlendAttachmentState defaultAttachment = config.colorBlendAttachments[0];
    config.colorBlendAttachments.assign(4, defaultAttachment);
    config.colorBlendInfo.attachmentCount = 4;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

    // 1. 茶壶管线
    geometryPipeline = std::make_unique<VulkanPipeline>(vulkanDevice, "../shaders/mrt.vert.spv", "../shaders/mrt.frag.spv", config);
    gridPipeline = std::make_unique<VulkanPipeline>(vulkanDevice, "../shaders/grid_mrt.vert.spv", "../shaders/grid_mrt.frag.spv", config);
    // 2. 天空盒管线
    //config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT; 
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyboxPipeline = std::make_unique<VulkanPipeline>(
        vulkanDevice, "../shaders/skybox_mrt.vert.spv", "../shaders/skybox_mrt.frag.spv", config);

  
}

void GeometrySystem::render(FrameInfo& frameInfo) 
{
    // ==========================================
    // 1. 强制恢复动态状态，切断 ImGui 的污染！
    // ==========================================
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    // 从 Swapchain 或 FrameInfo 中获取实际的渲染分辨率
    // 假设你的 frameInfo 里能拿到宽度和高度，或者传固定值测试
    viewport.width = 1920.0f;  // 请替换为你的实际 extent.width
    viewport.height = 1080.0f; // 请替换为你的实际 extent.height
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frameInfo.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = { 1920, 1080 }; // 同样替换为你的 extent
    vkCmdSetScissor(frameInfo.commandBuffer, 0, 1, &scissor);

    // ==========================================
    // 2. 绑定全局资源
    // ==========================================
    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);

    // ==========================================
    // 3. 实体渲染循环
    // ==========================================
    for (auto& obj : frameInfo.gameObjects) 
    {
        if (obj.model == nullptr) continue;

        // 无论是不是天空盒，都必须计算并推送属于它自己的矩阵！
        // 切断 Vulkan 的状态继承污染！
        SimplePushConstantData push{};
        push.modelMatrix = obj.transform.mat4();
        push.normalMatrix = obj.transform.mat4();

        if (obj.isSkybox) 
        {
            skyboxPipeline->bind(frameInfo.commandBuffer);
            
            vkCmdPushConstants(
                frameInfo.commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->bind(frameInfo.commandBuffer);
            obj.model->draw(frameInfo.commandBuffer);
        }
        else if (obj.isGrid) 
        {  // ✨ 新增网格分支
            gridPipeline->bind(frameInfo.commandBuffer);
            
            SimplePushConstantData push{};
            push.modelMatrix = obj.transform.mat4();
            push.normalMatrix = obj.transform.mat4();

            vkCmdPushConstants(
                frameInfo.commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->bind(frameInfo.commandBuffer);
            obj.model->draw(frameInfo.commandBuffer);
        } 
        else 
        {
            geometryPipeline->bind(frameInfo.commandBuffer);
            
            vkCmdPushConstants(
                frameInfo.commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->bind(frameInfo.commandBuffer);
            obj.model->draw(frameInfo.commandBuffer);
        }
    }
}