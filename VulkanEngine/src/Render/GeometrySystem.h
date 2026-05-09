#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Core/FrameInfo.h"
#include <memory>

class GeometrySystem
{
public:
	GeometrySystem(Device& device, VkRenderPass renderPass,
		VkDescriptorSetLayout globalSetLayout,
		VkDescriptorSetLayout materialSetLayout);
	~GeometrySystem();

	GeometrySystem(const GeometrySystem&) = delete;
	GeometrySystem& operator=(const GeometrySystem&) = delete;

	void render(FrameInfo& frameInfo);
	VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

private:
	void createPipelineLayout(VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout materialSetLayout);
	void createPipelines(VkRenderPass renderPass);

	Device& vulkanDevice;
	VkPipelineLayout pipelineLayout;
	std::unique_ptr<Pipeline> geometryPipeline;
	std::unique_ptr<Pipeline> skyboxPipeline;
	std::unique_ptr<Pipeline> gridPipeline;
};
