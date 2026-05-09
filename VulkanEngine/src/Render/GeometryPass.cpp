#include "GeometryPass.h"
#include "Core/DescriptorManager.h"
#include <array>
#include <stdexcept>

GeometryPass::GeometryPass(Device& device, GBuffer& gBuffer,
	VkDescriptorSetLayout globalSetLayout,
	VkDescriptorSetLayout materialSetLayout)
	: vulkanDevice(device), gBuffer(gBuffer),
	globalSetLayout(globalSetLayout), materialSetLayout(materialSetLayout)
{
}

GeometryPass::~GeometryPass()
{
	vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffer, nullptr);
	vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}

void GeometryPass::init()
{
	createRenderPass();
	createFramebuffer();
	geometrySystem = std::make_unique<GeometrySystem>(vulkanDevice, renderPass, globalSetLayout, materialSetLayout);
}

void GeometryPass::createRenderPass()
{
	std::array<VkAttachmentDescription, 5> attachments{};

	for (int i = 0; i < 4; i++)
	{
		attachments[i].format = (i == 0 || i == 1) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
		attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
	VkAttachmentReference depthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
	subpass.pColorAttachments = colorRefs.data();
	subpass.pDepthStencilAttachment = &depthRef;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
		throw std::runtime_error("GeometryPass: render pass failed!");
}

void GeometryPass::createFramebuffer()
{
	std::array<VkImageView, 5> attachments = {
		gBuffer.getPosition().view,
		gBuffer.getNormal().view,
		gBuffer.getAlbedo().view,
		gBuffer.getPbr().view,
		gBuffer.getDepth().view
	};

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = WIDTH;
	framebufferInfo.height = HEIGHT;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS)
		throw std::runtime_error("GeometryPass: framebuffer failed!");
}

void GeometryPass::execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo)
{
	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = framebuffer;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { WIDTH, HEIGHT };

	std::array<VkClearValue, 5> clearValues{};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
	clearValues[1].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
	clearValues[2].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
	clearValues[3].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
	clearValues[4].depthStencil = { 1.0f, 0 };
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	geometrySystem->render(frameInfo);
	vkCmdEndRenderPass(commandBuffer);
}
