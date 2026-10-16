#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "RHI/Device.h"
#include "RHI/GBuffer.h"
#include "Core/MaterialSlot.h"

class Material;

class DescriptorManager
{
public:
	void init(Device& device, uint32_t maxMaterials = 256);
	void cleanup();

	VkDescriptorSetLayout getGlobalSetLayout() const { return globalSetLayout; }
	VkDescriptorSetLayout getMaterialSetLayout() const { return materialSetLayout; }

	VkDescriptorSetLayout createSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

	// 全局描述符集：
	//  0=UBO  1=equirect 环境贴图(天空盒用)  2~5=G-Buffer  6=SSAO
	//  7=irradiance cubemap  8=prefiltered cubemap  9=BRDF LUT
	// 注意：D-2 之后光照通道的输出（HDR 中间目标）**不在**这里 ——
	// 它只在后效链内部流转，用各 Pass 自己的局部 set，不占用全局集。
	void buildGlobalSet(VkBuffer uboBuffer,
		const VkDescriptorImageInfo& envInfo,
		const GBuffer& gBuffer,
		VkSampler ssaoSampler, VkImageView ssaoView,
		const VkDescriptorImageInfo& irradianceInfo,
		const VkDescriptorImageInfo& prefilteredInfo,
		const VkDescriptorImageInfo& brdfLutInfo);
	VkDescriptorSet getGlobalSet() const { return globalSet; }

	// ---------------- 材质（D-3 起是 bindless 的）----------------
	//
	// 材质不再是"一个材质一个 VkDescriptorSet"，而是**一个 set 里放 4 个纹理数组**：
	//
	//     set 1, binding 0: sampler2D textures[capacity]   // albedo / normal /
	//     set 1, binding 1: sampler2D textures[capacity]   // metallicRoughness / ...
	//
	// 等等 —— 实际是 4 个独立数组，各自按同一个槽位下标索引：
	//     binding 0 = albedo 数组，binding 1 = normal，binding 2 = metallic，3 = roughness
	// 着色器用材质下标去采：  texture(albedoMaps[matId], uv)
	//
	// 为什么不用"每个材质一个 set"：那样每换一次材质就要 vkCmdBindDescriptorSets，
	// 场景里 104 个子网格 = 104 次绑定。bindless 之后**整帧只绑一次 set 1**，
	// 切材质只是改一个 push constant 里的整数。
	//
	// 槽位管理（分配/归还）保持不变 —— 仍然是"一次性预分配 + 空闲栈"，
	// 只是现在改的是数组里某个下标的内容，而不是某个独立 set 的内容。
	uint32_t allocateMaterialSet(const VkDescriptorImageInfo& albedo,
		const VkDescriptorImageInfo& normal,
		const VkDescriptorImageInfo& metallic,
		const VkDescriptorImageInfo& roughness);
	uint32_t allocateMaterialSet(const Material& material);

	// 归还槽位。越界或重复归还都会被安全忽略。
	//
	// ⚠️ bindless 之后归还语义变了：槽位被复用时是**改写数组里那个下标的内容**，
	// 而 GPU 可能还在读它。当前调用方只在初始化阶段分配（此时设备空闲），
	// 所以没问题；一旦将来要在运行期动态分配材质，必须在这里加"等帧 fence"
	// 或改用 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT + 双缓冲。
	void releaseMaterialSet(uint32_t index);

	// bindless 之后材质只有一个 set（不再是每个材质一个）。
	// 参数已经被忽略，保留这个重载只是因为调用点还写着 getMaterialSet(slot) ——
	// 想识别"哪些地方还在按材质逐个绑定"时，它是个好线索（现在是 0 个）。
	VkDescriptorSet getMaterialSet() const { return materialSet; }
	VkDescriptorSet getMaterialSet(uint32_t /*index*/) const { return materialSet; }

	// 槽位是否真的被绑定过内容。数组尾部允许部分未绑定
	//（descriptorBindingPartiallyBound），着色器那边靠 materialSet.alpha
	// 的守卫跳过——这里只用于 CPU 侧自检。
	bool isMaterialSlotWritten(uint32_t index) const
	{
		return index < slotInUse.size() && slotInUse[index];
	}

	uint32_t getMaterialSlotCapacity() const { return static_cast<uint32_t>(slotInUse.size()); }
	uint32_t getMaterialSlotUsed() const
	{
		return getMaterialSlotCapacity() - static_cast<uint32_t>(freeSlots.size());
	}

	VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);
	void writeSet(VkDescriptorSet set, const std::vector<VkWriteDescriptorSet>& writes);

	VkDescriptorPool getPool() const { return pool; }

private:
	void createGlobalLayout();
	void createMaterialLayout(uint32_t capacity);
	void writeMaterialSet(uint32_t slot,
		const VkDescriptorImageInfo& albedo,
		const VkDescriptorImageInfo& normal,
		const VkDescriptorImageInfo& metallic,
		const VkDescriptorImageInfo& roughness);

	Device* device = nullptr;
	VkDescriptorPool pool = VK_NULL_HANDLE;

	VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;

	VkDescriptorSet globalSet = VK_NULL_HANDLE;

	// D-3：材质只剩这一个 set，里面是 4 个纹理数组。
	// 数组长度（材质容量）在分配时通过 variable descriptor count 指定，
	// 所以布局里不写死 —— 想调容量只需改 init 的 maxMaterials。
	VkDescriptorSet materialSet = VK_NULL_HANDLE;
	uint32_t materialCapacity = 0;

	std::vector<bool> slotInUse;                 // 槽位占用标记
	std::vector<uint32_t> freeSlots;             // 空闲槽位栈，后进先出 -> O(1) 分配/归还
};
