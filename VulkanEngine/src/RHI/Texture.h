#pragma once

#include "Device.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

class Texture
{
public:
	Texture(Device& device, const std::string& filepath, VkFormat format, VkCommandPool commandPool, bool isHDR = false);

	Texture(Device& device, const void* pixelData, uint32_t width, uint32_t height,
		VkFormat format, VkCommandPool commandPool, bool isHDR = false);

	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(Texture&& other) noexcept;
	Texture& operator=(Texture&& other) noexcept;

	VkImageView getView() const { return view; }
	VkSampler getSampler() const { return sampler; }
	VkImage getImage() const { return image; }

private:
	void loadLDR(const std::string& filepath, VkFormat format, VkCommandPool commandPool);
	void loadHDR(const std::string& filepath, VkCommandPool commandPool);

	void createFromPixels(const void* pixels, uint32_t width, uint32_t height,
		VkDeviceSize pixelSize, VkFormat format, VkCommandPool commandPool,
		bool isFloat);

	Device& vulkanDevice;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
};
