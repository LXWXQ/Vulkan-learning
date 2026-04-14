#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Core/FrameInfo.h"
#include <memory>

class GeometrySystem 
{
public:
    GeometrySystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~GeometrySystem();

    GeometrySystem(const GeometrySystem&) = delete;
    GeometrySystem& operator=(const GeometrySystem&) = delete;
    void render(FrameInfo& frameInfo);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipelines(VkRenderPass renderPass);

    Device& vulkanDevice;
    VkPipelineLayout pipelineLayout;
    std::unique_ptr<Pipeline> geometryPipeline;
    std::unique_ptr<Pipeline> skyboxPipeline;
    std::unique_ptr<Pipeline> gridPipeline;
};