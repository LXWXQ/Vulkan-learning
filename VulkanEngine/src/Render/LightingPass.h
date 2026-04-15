#pragma once
#include "IRenderPass.h"
#include "RHI/Device.h"
#include "RHI/GBuffer.h"
#include "RHI/Swapchain.h"
#include "Render/LightingSystem.h"
#include "Core/ImGuiSystem.h" 
#include <memory>
#include <vector>

class LightingPass : public IRenderPass 
{
public:
    LightingPass(Device& device, GBuffer& gBuffer, Swapchain& swapchain, VkDescriptorSetLayout globalLayout);
    ~LightingPass() override;

    void init() override;
    void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) override;
    void onResize(VkExtent2D extent) override {}
    
    VkRenderPass getRenderPass() const { return renderPass; }
    void setImGuiSystem(ImGuiSystem* imgui) { imguiSystem = imgui; }
    void setDebugModel(std::shared_ptr<Model> model) { debugModel = model; }
    const std::string getName() const override { return "Lighting Pass"; }
private:
    void createRenderPass();
    void createFramebuffers();

    Device& vulkanDevice;
    GBuffer& gBuffer;
    Swapchain& vulkanSwapchain;
    VkDescriptorSetLayout globalSetLayout;

    std::unique_ptr<LightingSystem> lightingSystem;
    ImGuiSystem* imguiSystem = nullptr;
    std::shared_ptr<Model> debugModel = nullptr;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
};