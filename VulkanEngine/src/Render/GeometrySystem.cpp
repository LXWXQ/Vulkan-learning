#include "GeometrySystem.h"
#include "Core/DescriptorManager.h"
#include <stdexcept>

GeometrySystem::GeometrySystem(Device& device, VkRenderPass renderPass,
	VkDescriptorSetLayout globalSetLayout,
	VkDescriptorSetLayout materialSetLayout)
	: vulkanDevice(device)
{
	createPipelineLayout(globalSetLayout, materialSetLayout);
	createPipelines(renderPass);
}

GeometrySystem::~GeometrySystem()
{
	vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void GeometrySystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout materialSetLayout)
{
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(SimplePushConstantData);

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts{ globalSetLayout, materialSetLayout };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("GeometrySystem: pipeline layout failed!");
}

void GeometrySystem::createPipelines(VkRenderPass renderPass)
{
	PipelineConfigInfo config{};
	Pipeline::defaultPipelineConfigInfo(config, WIDTH, HEIGHT);
	config.renderPass = renderPass;
	config.pipelineLayout = pipelineLayout;
	config.subpass = 0;

	VkPipelineColorBlendAttachmentState defaultAttachment = config.colorBlendAttachments[0];
	config.colorBlendAttachments.assign(4, defaultAttachment);
	config.colorBlendInfo.attachmentCount = 4;
	config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

	geometryPipeline = std::make_unique<Pipeline>(vulkanDevice, "../shaders/mrt.vert.spv", "../shaders/mrt.frag.spv", config);
	gridPipeline = std::make_unique<Pipeline>(vulkanDevice, "../shaders/grid_mrt.vert.spv", "../shaders/grid_mrt.frag.spv", config);

	config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	config.depthStencilInfo.depthWriteEnable = VK_FALSE;
	config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	skyboxPipeline = std::make_unique<Pipeline>(vulkanDevice, "../shaders/skybox_mrt.vert.spv", "../shaders/skybox_mrt.frag.spv", config);
}

void GeometrySystem::render(FrameInfo& frameInfo)
{
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(WIDTH);
	viewport.height = static_cast<float>(HEIGHT);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(frameInfo.commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { WIDTH, HEIGHT };
	vkCmdSetScissor(frameInfo.commandBuffer, 0, 1, &scissor);

	VkDescriptorSet globalSet = frameInfo.descriptorManager->getGlobalSet();
	uint32_t lastMaterialIndex = UINT32_MAX;

	for (auto& obj : frameInfo.gameObjects)
	{
		if (obj.model == nullptr) continue;

		SimplePushConstantData push{};
		push.modelMatrix = obj.transform.mat4();
		push.normalMatrix = obj.transform.mat4();

		uint32_t matIdx = obj.materialSetIndex;
		if (obj.material && obj.material->descriptorSetIndex != UINT32_MAX)
			matIdx = obj.material->descriptorSetIndex;

		auto* pipe = &geometryPipeline;
		if (obj.isSkybox)
			pipe = &skyboxPipeline;
		else if (obj.isGrid)
			pipe = &gridPipeline;

		(*pipe)->bind(frameInfo.commandBuffer);

		vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SimplePushConstantData), &push);

		if (matIdx != lastMaterialIndex)
		{
			VkDescriptorSet matSet = frameInfo.descriptorManager->getMaterialSet(matIdx);
			VkDescriptorSet sets[] = { globalSet, matSet };
			vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout, 0, 2, sets, 0, nullptr);
			lastMaterialIndex = matIdx;
		}

		obj.model->bind(frameInfo.commandBuffer);
		obj.model->draw(frameInfo.commandBuffer);

		uint32_t indexCount = obj.model->getIndexCount();
		frameInfo.telemetry.drawCalls += 1;
		frameInfo.telemetry.triangles += indexCount / 3;
		frameInfo.telemetry.vertices += obj.model->getVertexCount();
	}
}
