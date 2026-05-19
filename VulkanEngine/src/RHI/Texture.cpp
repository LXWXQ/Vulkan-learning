#include "Texture.h"
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <stb_image.h>

Texture::Texture(Device& device, const std::string& filepath, VkFormat format, VkCommandPool commandPool, bool isHDR)
	: vulkanDevice(device)
{
	if (isHDR)
		loadHDR(filepath, commandPool);
	else
		loadLDR(filepath, format, commandPool);
}

Texture::Texture(Device& device, const void* pixelData, uint32_t width, uint32_t height,
	VkFormat format, VkCommandPool commandPool, bool isHDR)
	: vulkanDevice(device)
{
	VkDeviceSize pixelSize = isHDR ? sizeof(float) : 1;
	createFromPixels(pixelData, width, height, pixelSize, format, commandPool, isHDR);
}

Texture::~Texture()
{
	if (sampler != VK_NULL_HANDLE) vkDestroySampler(vulkanDevice.getDevice(), sampler, nullptr);
	if (view != VK_NULL_HANDLE) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
	if (image != VK_NULL_HANDLE) vmaDestroyImage(vulkanDevice.getVmaAllocator(), image, allocation);
}

Texture::Texture(Texture&& other) noexcept
	: vulkanDevice(other.vulkanDevice), image(other.image), allocation(other.allocation), view(other.view), sampler(other.sampler)
{
	other.image = VK_NULL_HANDLE;
	other.allocation = VK_NULL_HANDLE;
	other.view = VK_NULL_HANDLE;
	other.sampler = VK_NULL_HANDLE;
}

Texture& Texture::operator=(Texture&& other) noexcept
{
	if (this != &other)
	{
		if (sampler) vkDestroySampler(vulkanDevice.getDevice(), sampler, nullptr);
		if (view) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
		if (image) vmaDestroyImage(vulkanDevice.getVmaAllocator(), image, allocation);

		image = other.image;
		allocation = other.allocation;
		view = other.view;
		sampler = other.sampler;

		other.image = VK_NULL_HANDLE;
		other.allocation = VK_NULL_HANDLE;
		other.view = VK_NULL_HANDLE;
		other.sampler = VK_NULL_HANDLE;
	}
	return *this;
}

void Texture::loadLDR(const std::string& filepath, VkFormat format, VkCommandPool commandPool)
{
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
		throw std::runtime_error("Texture: failed to load " + filepath);

	createFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
		1, format, commandPool, false);

	stbi_image_free(pixels);

	std::cout << "[Texture] " << filepath << " (" << texWidth << "x" << texHeight << ")\n";
}

void Texture::loadHDR(const std::string& filepath, VkCommandPool commandPool)
{
	int texWidth, texHeight, texChannels;
	float* pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
		throw std::runtime_error("Texture: failed to load HDR " + filepath);

	createFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
		sizeof(float), VK_FORMAT_R32G32B32A32_SFLOAT, commandPool, true);

	stbi_image_free(pixels);

	std::cout << "[Texture] HDR " << filepath << " (" << texWidth << "x" << texHeight << ")\n";
}

void Texture::createFromPixels(const void* pixels, uint32_t width, uint32_t height,
	VkDeviceSize pixelSize, VkFormat format, VkCommandPool commandPool,
	bool isFloat)
{
	VkDeviceSize imageSize = width * height * 4 * pixelSize;

	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo{};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	vmaCreateBuffer(vulkanDevice.getVmaAllocator(), &bufferInfo, &stagingAllocInfo,
		&stagingBuffer, &stagingAllocation, nullptr);

	void* data;
	vmaMapMemory(vulkanDevice.getVmaAllocator(), stagingAllocation, &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vmaUnmapMemory(vulkanDevice.getVmaAllocator(), stagingAllocation);

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	VmaAllocationCreateInfo imageAllocInfo{};
	imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	vmaCreateImage(vulkanDevice.getVmaAllocator(), &imageInfo, &imageAllocInfo,
		&this->image, &this->allocation, nullptr);

	VkCommandBufferAllocateInfo allocInfoCmd{};
	allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfoCmd.commandPool = commandPool;
	allocInfoCmd.commandBufferCount = 1;
	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfoCmd, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = this->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { width, height, 1 };
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, this->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(vulkanDevice.getGraphicsQueue());
	vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool, 1, &commandBuffer);

	vmaDestroyBuffer(vulkanDevice.getVmaAllocator(), stagingBuffer, stagingAllocation);

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = this->image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	vkCreateImageView(vulkanDevice.getDevice(), &viewInfo, nullptr, &this->view);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16.0f;
	samplerInfo.borderColor = isFloat ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &this->sampler);
}
