#include "DescriptorManager.h"
#include "Material.h"
#include "FrameInfo.h"
#include <stdexcept>
#include <iostream>

void DescriptorManager::init(Device& dev, uint32_t maxMaterials)
{
	device = &dev;
	maxMaterialSlots = maxMaterials;

	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         100 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2000 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = maxMaterials + 16;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: pool creation failed!");

	createGlobalLayout();
	createMaterialLayout();

	materialSets.resize(maxMaterialSlots, VK_NULL_HANDLE);
	materialUsed.resize(maxMaterialSlots, false);

	std::cout << "[DescriptorManager] Arena ready, " << maxMaterialSlots << " material slots.\n";
}

void DescriptorManager::cleanup()
{
	if (globalSetLayout) vkDestroyDescriptorSetLayout(device->getDevice(), globalSetLayout, nullptr);
	if (materialSetLayout) vkDestroyDescriptorSetLayout(device->getDevice(), materialSetLayout, nullptr);
	if (pool) vkDestroyDescriptorPool(device->getDevice(), pool, nullptr);
}

void DescriptorManager::createGlobalLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> bindings(7);

	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	for (uint32_t i = 1; i <= 6; i++)
	{
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: global layout failed!");
}

void DescriptorManager::createMaterialLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> bindings(4);

	for (uint32_t i = 0; i < 4; i++)
	{
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &materialSetLayout) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: material layout failed!");
}

VkDescriptorSetLayout DescriptorManager::createSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout layout;
	if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: custom layout failed!");
	return layout;
}

void DescriptorManager::buildGlobalSet(VkBuffer uboBuffer,
	const VkDescriptorImageInfo& envInfo,
	const GBuffer& gBuffer,
	VkSampler ssaoSampler, VkImageView ssaoView)
{
	globalSet = allocateSet(globalSetLayout);

	VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, sizeof(GlobalUbo) };

	VkDescriptorImageInfo posInfo{ gBuffer.getSampler(), gBuffer.getPosition().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo normInfo{ gBuffer.getSampler(), gBuffer.getNormal().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo albInfo{ gBuffer.getSampler(), gBuffer.getAlbedo().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo pbrInfo{ gBuffer.getSampler(), gBuffer.getPbr().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo ssaoInfo{ ssaoSampler, ssaoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	VkDescriptorImageInfo envCopy = envInfo;

	VkDescriptorImageInfo imageInfos[] = {
		envCopy, posInfo, normInfo, albInfo, pbrInfo, ssaoInfo
	};

	std::vector<VkWriteDescriptorSet> writes(7);

	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = globalSet;
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].descriptorCount = 1;
	writes[0].pBufferInfo = &uboInfo;

	for (int i = 0; i < 6; i++)
	{
		writes[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i + 1].dstSet = globalSet;
		writes[i + 1].dstBinding = static_cast<uint32_t>(i + 1);
		writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i + 1].descriptorCount = 1;
		writes[i + 1].pImageInfo = &imageInfos[i];
	}

	vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

uint32_t DescriptorManager::allocateMaterialSet(const VkDescriptorImageInfo& albedo,
	const VkDescriptorImageInfo& normal,
	const VkDescriptorImageInfo& metallic,
	const VkDescriptorImageInfo& roughness)
{
	for (uint32_t i = 0; i < maxMaterialSlots; i++)
	{
		if (!materialUsed[i])
		{
			materialSets[i] = allocateSet(materialSetLayout);
			materialUsed[i] = true;

			std::vector<VkWriteDescriptorSet> writes(4);
			VkDescriptorImageInfo* infos[] = {
				const_cast<VkDescriptorImageInfo*>(&albedo),
				const_cast<VkDescriptorImageInfo*>(&normal),
				const_cast<VkDescriptorImageInfo*>(&metallic),
				const_cast<VkDescriptorImageInfo*>(&roughness)
			};

			for (int j = 0; j < 4; j++)
			{
				writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[j].dstSet = materialSets[i];
				writes[j].dstBinding = static_cast<uint32_t>(j);
				writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[j].descriptorCount = 1;
				writes[j].pImageInfo = infos[j];
			}

			vkUpdateDescriptorSets(device->getDevice(), 4, writes.data(), 0, nullptr);
			return i;
		}
	}

	throw std::runtime_error("DescriptorManager: material slots exhausted!");
}

VkDescriptorSet DescriptorManager::getMaterialSet(uint32_t index) const
{
	return (index < materialSets.size()) ? materialSets[index] : VK_NULL_HANDLE;
}

uint32_t DescriptorManager::allocateMaterialSet(const Material& material)
{
	return allocateMaterialSet(
		material.getAlbedoInfo(),
		material.getNormalInfo(),
		material.getMetallicRoughnessInfo(),
		material.getMetallicRoughnessInfo());
}

VkDescriptorSet DescriptorManager::allocateSet(VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet set;
	if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &set) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: set allocation failed!");
	return set;
}

void DescriptorManager::writeSet(VkDescriptorSet set, const std::vector<VkWriteDescriptorSet>& writes)
{
	for (auto& w : const_cast<std::vector<VkWriteDescriptorSet>&>(writes))
		w.dstSet = set;
	vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
