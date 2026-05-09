#include "Render/SSAOPass.h"
#include <random>
#include <stdexcept>
#include <cstring>
#include <array>

SSAOPass::SSAOPass(Device& device, VkExtent2D extent, VkCommandPool commandPool,
	VkDescriptorSetLayout globalLayout, DescriptorManager* descMgr)
	: PostProcessPass(device, extent, VK_FORMAT_R8_UNORM),
	commandPool(commandPool), globalSetLayout(globalLayout), descriptorManager(descMgr)
{
}

SSAOPass::~SSAOPass()
{
	vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device.getDevice(), localSetLayout, nullptr);

	vkDestroyBuffer(device.getDevice(), uboBuffer, nullptr);
	vkFreeMemory(device.getDevice(), uboMemory, nullptr);

	vkDestroySampler(device.getDevice(), noiseTexture.sampler, nullptr);
	vkDestroyImageView(device.getDevice(), noiseTexture.view, nullptr);
	vkDestroyImage(device.getDevice(), noiseTexture.image, nullptr);
	vkFreeMemory(device.getDevice(), noiseTexture.memory, nullptr);
}

void SSAOPass::init()
{
	PostProcessPass::init();

	generateSampleKernel();
	createNoiseTexture();
	createUniformBuffer();
	createLocalDescriptors();
	createPipeline();
}

void SSAOPass::generateSampleKernel()
{
	std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	std::default_random_engine generator;

	for (int i = 0; i < 16; ++i)
	{
		glm::vec3 sample(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator)
		);
		sample = glm::normalize(sample);
		sample *= randomFloats(generator);

		float scale = float(i) / 16.0f;
		scale = glm::mix(0.1f, 1.0f, scale * scale);
		sample *= scale;

		ssaoKernel.push_back(glm::vec4(sample, 0.0f));
	}
}

void SSAOPass::createNoiseTexture()
{
	std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	std::default_random_engine generator;
	std::vector<glm::vec4> ssaoNoise;

	for (int i = 0; i < 16; i++)
		ssaoNoise.push_back(glm::vec4(randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f, 0.0f, 1.0f));

	VkDeviceSize imageSize = ssaoNoise.size() * sizeof(glm::vec4);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &stagingBuffer);

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device.getDevice(), stagingBuffer, &memReqs);
	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &stagingBufferMemory);
	vkBindBufferMemory(device.getDevice(), stagingBuffer, stagingBufferMemory, 0);

	void* data;
	vkMapMemory(device.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, ssaoNoise.data(), (size_t)imageSize);
	vkUnmapMemory(device.getDevice(), stagingBufferMemory);

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = 4;
	imageInfo.extent.height = 4;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	vkCreateImage(device.getDevice(), &imageInfo, nullptr, &noiseTexture.image);

	vkGetImageMemoryRequirements(device.getDevice(), noiseTexture.image, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &noiseTexture.memory);
	vkBindImageMemory(device.getDevice(), noiseTexture.image, noiseTexture.memory, 0);

	VkCommandBufferAllocateInfo allocCmdInfo{};
	allocCmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocCmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocCmdInfo.commandPool = commandPool;
	allocCmdInfo.commandBufferCount = 1;
	VkCommandBuffer cmdBuffer;
	vkAllocateCommandBuffers(device.getDevice(), &allocCmdInfo, &cmdBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmdBuffer, &beginInfo);

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = noiseTexture.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { 4, 4, 1 };
	vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, noiseTexture.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	vkEndCommandBuffer(cmdBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmdBuffer;
	vkQueueSubmit(device.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(device.getGraphicsQueue());
	vkFreeCommandBuffers(device.getDevice(), commandPool, 1, &cmdBuffer);

	vkDestroyBuffer(device.getDevice(), stagingBuffer, nullptr);
	vkFreeMemory(device.getDevice(), stagingBufferMemory, nullptr);

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = noiseTexture.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &noiseTexture.view);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &noiseTexture.sampler);
}

void SSAOPass::createUniformBuffer()
{
	VkDeviceSize bufferSize = sizeof(SSAOUbo);
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &uboBuffer);

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device.getDevice(), uboBuffer, &memReqs);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &uboMemory);
	vkBindBufferMemory(device.getDevice(), uboBuffer, uboMemory, 0);
	vkMapMemory(device.getDevice(), uboMemory, 0, bufferSize, 0, &uboMapped);
}

void SSAOPass::createLocalDescriptors()
{
	std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	localSetLayout = descriptorManager->createSetLayout(
		std::vector<VkDescriptorSetLayoutBinding>(bindings.begin(), bindings.end()));

	localSet = descriptorManager->allocateSet(localSetLayout);

	VkDescriptorBufferInfo bufferInfo{ uboBuffer, 0, sizeof(SSAOUbo) };
	VkDescriptorImageInfo noiseInfo{ noiseTexture.sampler, noiseTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	std::array<VkWriteDescriptorSet, 2> writes{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = localSet;
	writes[0].dstBinding = 0;
	writes[0].dstArrayElement = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].descriptorCount = 1;
	writes[0].pBufferInfo = &bufferInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = localSet;
	writes[1].dstBinding = 1;
	writes[1].dstArrayElement = 0;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[1].descriptorCount = 1;
	writes[1].pImageInfo = &noiseInfo;

	vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void SSAOPass::createPipeline()
{
	std::array<VkDescriptorSetLayout, 2> layouts = { globalSetLayout, localSetLayout };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
	pipelineLayoutInfo.pSetLayouts = layouts.data();
	vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout);

	PipelineConfigInfo config{};
	Pipeline::defaultPipelineConfigInfo(config, currentExtent.width, currentExtent.height);
	config.renderPass = renderPass;
	config.pipelineLayout = pipelineLayout;
	config.subpass = 0;

	config.bindingDescriptions.clear();
	config.attributeDescriptions.clear();
	config.depthStencilInfo.depthTestEnable = VK_FALSE;
	config.depthStencilInfo.depthWriteEnable = VK_FALSE;

	ssaoPipeline = std::make_unique<Pipeline>(device, "../shaders/fullscreen.vert.spv", "../shaders/ssao.frag.spv", config);
}

void SSAOPass::execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo)
{
	SSAOUbo ubo{};
	ubo.projection = frameInfo.projectionMatrix;
	ubo.view = frameInfo.viewMatrix;
	ubo.radius = frameInfo.settings.ssaoRadius;
	ubo.bias = frameInfo.settings.ssaoBias;
	ubo.kernelSize = frameInfo.settings.ssaoKernelSize;
	ubo.enabled = frameInfo.settings.ssaoEnabled ? 1.0f : 0.0f;

	memcpy(ubo.samples, ssaoKernel.data(), 16 * sizeof(glm::vec4));
	memcpy(uboMapped, &ubo, sizeof(ubo));

	VkRenderPassBeginInfo passInfo{};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = renderPass;
	passInfo.framebuffer = framebuffer;
	passInfo.renderArea.offset = { 0, 0 };
	passInfo.renderArea.extent = currentExtent;

	VkClearValue clearColor = { {{1.0f, 1.0f, 1.0f, 1.0f}} };
	passInfo.clearValueCount = 1;
	passInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

	ssaoPipeline->bind(commandBuffer);

	VkDescriptorSet globalSet = frameInfo.descriptorManager->getGlobalSet();
	VkDescriptorSet sets[] = { globalSet, localSet };
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipelineLayout, 0, 2, sets, 0, nullptr);

	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}
