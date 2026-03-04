#pragma once
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "FrameInfo.hpp"
#include <memory>

class GeometrySystem {
public:
    // 构造时，自己负责创建自己的 PipelineLayout 和 Pipeline
    GeometrySystem(VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~GeometrySystem();

    GeometrySystem(const GeometrySystem&) = delete;
    GeometrySystem& operator=(const GeometrySystem&) = delete;

    // 唯一的对外接口
    void render(FrameInfo& frameInfo);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipelines(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    VkPipelineLayout pipelineLayout;
    std::unique_ptr<VulkanPipeline> geometryPipeline;
    std::unique_ptr<VulkanPipeline> skyboxPipeline;
};