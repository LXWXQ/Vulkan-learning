#include "DescriptorManager.h"
#include "Material.h"
#include "FrameInfo.h"
#include <stdexcept>
#include <iostream>
#include <string>

void DescriptorManager::init(Device& dev, uint32_t maxMaterials)
{
	device = &dev;
	materialCapacity = maxMaterials;

	// 池容量按 maxMaterials 推算，不再硬编码 —— 否则把 maxMaterials 调大时，
	// 池会在 vkCreateDescriptorPool 阶段直接失败。
	// 除材质外还要容纳：全局 set 1 个、SSAO local 1 个、Bloom 7 个
	//（1 亮部提取 + 3 降采样 + 3 上采样）、Present 1 个 —— D-2 起后效链
	// 每个 Pass 都有自己的 set，所以这里的余量必须跟着涨。
	//
	// D-3 变化：材质从"每材质 4 个独立 descriptor"变成"4 个数组各 maxMaterials 项"，
	// 描述符总数不变（还是 maxMaterials * 4），但 **set 数从 maxMaterials 降到 1**。
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         maxMaterials + 16 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxMaterials * 4 + 96 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = maxMaterials + 64;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: pool creation failed!");

	createGlobalLayout();
	createMaterialLayout(maxMaterials);

	// ---- D-3：材质 set 只申请一个，长度 = 材质容量 ----
	//
	// 用 VARIABLE_DESCRIPTOR_COUNT 让数组长度在**分配时**决定，而不是写死在布局里。
	// 好处：想调材质容量只需改 init 的 maxMaterials，布局不用动，
	// 也不用为不同容量维护多套布局。
	slotInUse.resize(maxMaterials, false);
	freeSlots.reserve(maxMaterials);

	VkDescriptorSetVariableDescriptorCountAllocateInfo variableCount{};
	variableCount.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
	variableCount.descriptorSetCount = 1;
	variableCount.pDescriptorCounts = &maxMaterials;

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &materialSetLayout;
	allocInfo.pNext = &variableCount;

	if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &materialSet) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: bindless 材质 set 分配失败");

	// 倒序压栈：这样第一次 allocate 拿到的是槽位 0，便于调试时对照
	for (uint32_t i = maxMaterials; i > 0; --i)
		freeSlots.push_back(i - 1);

	std::cout << "[DescriptorManager] Bindless arena ready: 1 set, "
		<< maxMaterials << " material slots ("
		<< (maxMaterials * 4) << " sampler descriptors in 4 arrays).\n";
}

void DescriptorManager::cleanup()
{
	// VkDescriptorSet 会随描述符池一起销毁，这里只需要清掉索引表
	slotInUse.clear();
	freeSlots.clear();
	globalSet = VK_NULL_HANDLE;
	materialSet = VK_NULL_HANDLE;
	materialCapacity = 0;

	if (globalSetLayout) vkDestroyDescriptorSetLayout(device->getDevice(), globalSetLayout, nullptr);
	if (materialSetLayout) vkDestroyDescriptorSetLayout(device->getDevice(), materialSetLayout, nullptr);
	if (pool) vkDestroyDescriptorPool(device->getDevice(), pool, nullptr);
}

void DescriptorManager::createGlobalLayout()
{
	// binding 0 = UBO，binding 1~9 = 9 张贴图：
	//   1 equirect 环境贴图（天空盒）
	//   2~5 G-Buffer（位置/法线/albedo/PBR）
	//   6 SSAO
	//   7 irradiance cubemap（漫反射 IBL）
	//   8 prefiltered cubemap（镜面 IBL）
	//   9 BRDF 积分表
	std::vector<VkDescriptorSetLayoutBinding> bindings(10);

	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	for (uint32_t i = 1; i <= 9; i++)
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

void DescriptorManager::createMaterialLayout(uint32_t capacity)
{
	// D-3 bindless：4 个**变长纹理数组**，各自被同一个材质下标索引。
	//
	// 描述符数组 + 运行时索引是 bindless 的核心：着色器里写
	//     texture(albedoMaps[matId], uv)
	// 其中 matId 来自 push constant，同一个 draw call 里不同像素可以用不同下标
	//（这就是为什么必须开 shaderSampledImageArrayNonUniformIndexing）。
	//
	// descriptorCount 在这里填 0 是不行的（规范要求至少 1）；
	// 真实长度交给 VkDescriptorSetVariableDescriptorCountAllocateInfo 在分配时指定。
	//
	// ⚠️ VARIABLE_DESCRIPTOR_COUNT_BIT 只能加在**编号最大的那个 binding** 上
	//（VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-pBindingFlags-03004）。
	// 所以这里不能四个数组都加 —— 加了会直接被验证层拒绝。
	// 四个数组用同一个材质下标、长度自然也相同，只在最后一个 binding 声明即可。
	std::vector<VkDescriptorSetLayoutBinding> bindings(4);

	// 数组里可能有"还没写过内容"的下标（材质槽位是复用池），
	// 允许部分绑定，避免必须把所有槽位先填一张占位图。
	// 这个标志是"每个 binding 各自算"的，所以四个都要加。
	std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
	flags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
	flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	flagsInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	flagsInfo.pBindingFlags = flags.data();

	// descriptorCount 这里是**上限**，不是最终长度：
	// 分配时再用 VkDescriptorSetVariableDescriptorCountAllocateInfo 指定实际长度，
	// 而规范要求"实际长度 <= 布局里声明的值"（VUID-...-pSetLayouts-03046）。
	// 所以上限就写 capacity —— 反正描述符池已经按它开好了。
	for (uint32_t i = 0; i < 4; i++)
	{
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = capacity;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();
	layoutInfo.pNext = &flagsInfo;

	if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &materialSetLayout) != VK_SUCCESS)
		throw std::runtime_error("DescriptorManager: bindless 材质布局创建失败");
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
	VkSampler ssaoSampler, VkImageView ssaoView,
	const VkDescriptorImageInfo& irradianceInfo,
	const VkDescriptorImageInfo& prefilteredInfo,
	const VkDescriptorImageInfo& brdfLutInfo)
{
	globalSet = allocateSet(globalSetLayout);

	VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, sizeof(GlobalUbo) };

	VkDescriptorImageInfo posInfo{ gBuffer.getSampler(), gBuffer.getPosition().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo normInfo{ gBuffer.getSampler(), gBuffer.getNormal().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo albInfo{ gBuffer.getSampler(), gBuffer.getAlbedo().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo pbrInfo{ gBuffer.getSampler(), gBuffer.getPbr().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo ssaoInfo{ ssaoSampler, ssaoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	VkDescriptorImageInfo envCopy = envInfo;
	VkDescriptorImageInfo irradianceCopy = irradianceInfo;
	VkDescriptorImageInfo prefilteredCopy = prefilteredInfo;
	VkDescriptorImageInfo brdfLutCopy = brdfLutInfo;

	// 顺序必须和 createGlobalLayout 里的 binding 号一一对应
	VkDescriptorImageInfo imageInfos[] = {
		envCopy, posInfo, normInfo, albInfo, pbrInfo, ssaoInfo,
		irradianceCopy, prefilteredCopy, brdfLutCopy
	};

	std::vector<VkWriteDescriptorSet> writes(10);

	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = globalSet;
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].descriptorCount = 1;
	writes[0].pBufferInfo = &uboInfo;

	for (int i = 0; i < 9; i++)
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
	if (freeSlots.empty())
		throw std::runtime_error("DescriptorManager: material slots exhausted! capacity = "
			+ std::to_string(materialCapacity));

	const uint32_t slot = freeSlots.back();
	freeSlots.pop_back();
	slotInUse[slot] = true;

	writeMaterialSet(slot, albedo, normal, metallic, roughness);
	return slot;
}

void DescriptorManager::releaseMaterialSet(uint32_t index)
{
	if (index >= slotInUse.size() || !slotInUse[index])
		return;                 // 越界或重复归还：安全忽略

	slotInUse[index] = false;
	freeSlots.push_back(index);
}

void DescriptorManager::writeMaterialSet(uint32_t slot,
	const VkDescriptorImageInfo& albedo,
	const VkDescriptorImageInfo& normal,
	const VkDescriptorImageInfo& metallic,
	const VkDescriptorImageInfo& roughness)
{
	// bindless 之后"写材质"= 更新数组里下标 slot 那一项。
	// 注意 dstArrayElement：它指定写数组的第几个元素。
	const VkDescriptorImageInfo* infos[4] = { &albedo, &normal, &metallic, &roughness };

	std::vector<VkWriteDescriptorSet> writes(4);
	for (int j = 0; j < 4; j++)
	{
		writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[j].dstSet = materialSet;
		writes[j].dstBinding = static_cast<uint32_t>(j);
		writes[j].dstArrayElement = slot;
		writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[j].descriptorCount = 1;
		writes[j].pImageInfo = infos[j];
	}

	vkUpdateDescriptorSets(device->getDevice(), 4, writes.data(), 0, nullptr);
}

uint32_t DescriptorManager::allocateMaterialSet(const Material& material)
{
	// metallicRoughness 贴图同时写进 metallic / roughness 两个数组：
	// mrt.frag 从 metallicMaps[id].b 取金属度、从 roughnessMaps[id].g 取粗糙度，
	// 两者本来就是同一张 glTF 打包贴图。
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
