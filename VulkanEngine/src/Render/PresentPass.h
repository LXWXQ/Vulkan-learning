#pragma once
#include "IRenderPass.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/RenderGraph.h"
#include "RHI/Swapchain.h"
#include "Render/RenderResources.h"
#include "Core/DescriptorManager.h"
#include "Core/ImGuiSystem.h"
#include <memory>
#include <array>
#include "Core/FrameInFlight.h"

// Final post-processing pass:
// HDR scene + Bloom -> camera motion blur -> exposure -> ACES -> gamma ->
// swapchain. ImGui is drawn after the scene so UI never gets blurred.
class PresentPass : public IRenderPass
{
public:
    PresentPass(Device& device, Swapchain& swapchain, RenderGraph& graph,
                RenderResources& resources, DescriptorManager* descMgr,
                VkDescriptorSetLayout globalSetLayout);
    ~PresentPass() override;

    void init() override;
    void addToGraph(RenderGraph& graph, FrameInfo& frameInfo) override;
    void resize(VkExtent2D extent) override;
    std::string getName() const override { return "Present"; }

    void setImGuiSystem(ImGuiSystem* imgui) { imguiSystem = imgui; }

private:
    struct PresentPush
    {
        float exposure;
        float bloomIntensity;
        float bloomEnabled;
        float motionBlurEnabled;
        float motionBlurStrength;
        float motionBlurMaxRadius;
        float motionBlurFrame;
        float pad;
    };
    static_assert(sizeof(PresentPush) == 32,
        "PresentPush must match present.frag/present_no_bloom.frag");

    void createDescriptorLayout();
    void createPipeline();
    void updateBloomDescriptor(uint32_t frame);
    void updateNoBloomDescriptor(uint32_t frame);

    Device& device;
    Swapchain& swapchain;
    RenderGraph& renderGraph;
    RenderResources& resources;
    DescriptorManager* descriptorManager;

    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler positionSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout noBloomSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> sets{};
    std::array<VkDescriptorSet, kFramesInFlight> noBloomSets{};
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout noBloomPipelineLayout = VK_NULL_HANDLE;
    VkFormat pipelineSwapchainFormat = VK_FORMAT_UNDEFINED;
    std::unique_ptr<Pipeline> pipeline;
    std::unique_ptr<Pipeline> noBloomPipeline;

    ImGuiSystem* imguiSystem = nullptr;
};
