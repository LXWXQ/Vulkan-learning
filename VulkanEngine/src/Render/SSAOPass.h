#pragma once
#include "Render/PostProcessPass.h"
#include "RHI/Pipeline.h"
#include "Core/DescriptorManager.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

struct SSAOUbo
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::vec4 samples[64];
	int kernelSize = 64;
	float radius = 0.5f;
	float bias = 0.025f;
	float enabled = 1.0f;
};

class SSAOPass : public PostProcessPass
{
public:
	SSAOPass(Device& device, VkExtent2D extent, VkCommandPool commandPool,
		VkDescriptorSetLayout globalLayout, DescriptorManager* descMgr);
	~SSAOPass() override;

	void init() override;
	void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) override;
	const std::string getName() const override { return "SSAO Pass"; }

private:
	void generateSampleKernel();
	void createNoiseTexture();
	void createUniformBuffer();
	void createLocalDescriptors();
	void createPipeline();

	VkCommandPool commandPool;
	VkDescriptorSetLayout globalSetLayout;
	DescriptorManager* descriptorManager;

	std::vector<glm::vec4> ssaoKernel;

	struct {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
	} noiseTexture;

	VkBuffer uboBuffer = VK_NULL_HANDLE;
	VkDeviceMemory uboMemory = VK_NULL_HANDLE;
	void* uboMapped = nullptr;

	VkDescriptorSetLayout localSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet localSet = VK_NULL_HANDLE;

	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::unique_ptr<Pipeline> ssaoPipeline;
};
