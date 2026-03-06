#pragma once
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "FrameInfo.hpp"
#include <memory>

class GeometrySystem 
{
public:
    GeometrySystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~GeometrySystem();

    GeometrySystem(const GeometrySystem&) = delete;
    GeometrySystem& operator=(const GeometrySystem&) = delete;
    void render(FrameInfo& frameInfo);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipelines(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    VkPipelineLayout pipelineLayout;
    std::unique_ptr<VulkanPipeline> geometryPipeline;
    std::unique_ptr<VulkanPipeline> skyboxPipeline;
    std::unique_ptr<VulkanPipeline> gridPipeline;
};