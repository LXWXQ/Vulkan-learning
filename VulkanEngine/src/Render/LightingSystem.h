#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Core/FrameInfo.h"
#include <memory>

class LightingSystem 
{
public:
    LightingSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~LightingSystem();

    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;

    void render(FrameInfo& frameInfo);
    void createDebugLightPipeline(VkRenderPass renderPass);
    void renderDebugLights(FrameInfo& frameInfo,std::shared_ptr<Model> sphereModel);
private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipeline(VkRenderPass renderPass);

    Device& vulkanDevice;
    VkPipelineLayout pipelineLayout;
    std::unique_ptr<Pipeline> lightingPipeline;
    std::unique_ptr<Pipeline> debugLightPipeline;
};