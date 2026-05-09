#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "RHI/Device.h"
#include "RHI/GBuffer.h"

class Material;

class DescriptorManager
{
public:
	void init(Device& device, uint32_t maxMaterials = 256);
	void cleanup();

	VkDescriptorSetLayout getGlobalSetLayout() const { return globalSetLayout; }
	VkDescriptorSetLayout getMaterialSetLayout() const { return materialSetLayout; }

	VkDescriptorSetLayout createSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

	void buildGlobalSet(VkBuffer uboBuffer,
		const VkDescriptorImageInfo& envInfo,
		const GBuffer& gBuffer,
		VkSampler ssaoSampler, VkImageView ssaoView);
	VkDescriptorSet getGlobalSet() const { return globalSet; }

	uint32_t allocateMaterialSet(const VkDescriptorImageInfo& albedo,
		const VkDescriptorImageInfo& normal,
		const VkDescriptorImageInfo& metallic,
		const VkDescriptorImageInfo& roughness);
	uint32_t allocateMaterialSet(const Material& material);
	VkDescriptorSet getMaterialSet(uint32_t index) const;

	VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);
	void writeSet(VkDescriptorSet set, const std::vector<VkWriteDescriptorSet>& writes);

	VkDescriptorPool getPool() const { return pool; }

private:
	void createGlobalLayout();
	void createMaterialLayout();

	Device* device = nullptr;
	VkDescriptorPool pool = VK_NULL_HANDLE;

	VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;

	VkDescriptorSet globalSet = VK_NULL_HANDLE;

	std::vector<VkDescriptorSet> materialSets;
	std::vector<bool> materialUsed;
	uint32_t maxMaterialSlots = 0;
};
