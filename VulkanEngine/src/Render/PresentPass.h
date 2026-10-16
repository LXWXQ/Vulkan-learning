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

// ---------------------------------------------------------------------------
// D-2: PresentPass —— 后效链的终点
//
// 顺序：HDR 场景 + Bloom*强度  ->  曝光  ->  ACES 色调映射  ->  gamma  ->  交换链
//
// 为什么把它单独拆成一个 Pass（而不是继续留在 LightingPass 里）：
//   1. 光照通道现在输出线性 HDR，交换链是 LDR，两者之间必须有人做转换；
//   2. 所有"显示前"的操作（tonemap / gamma / 未来的颜色分级、暗角、锐化、
//      ImGui 叠加）天然属于同一步，集中在一条全屏绘制里最省；
//   3. ImGui 必须画在最终图像之上 —— 放在这里之后，任何新增的 Pass
//      只要插在 Present 之前就自动被 UI 覆盖，不会出现"UI 被后效糊掉"。
// ---------------------------------------------------------------------------
class PresentPass : public IRenderPass
{
public:
    PresentPass(Device& device, Swapchain& swapchain, RenderGraph& graph,
                RenderResources& resources, DescriptorManager* descMgr);
    ~PresentPass() override;

    void init() override;
    void addToGraph(RenderGraph& graph, FrameInfo& frameInfo) override;
    std::string getName() const override { return "Present"; }

    // ImGui 的"构建 UI + Render()"必须在 Present 之前完成，
    // 但 ImGui 的绘制指令要画在 Present 的动态渲染作用域里。
    void setImGuiSystem(ImGuiSystem* imgui) { imguiSystem = imgui; }

private:
    struct PresentPush
    {
        float exposure;
        float bloomIntensity;
        float bloomEnabled;
        float pad;
    };

    void createDescriptorLayout();
    void createPipeline();

    Device& device;
    Swapchain& swapchain;
    RenderGraph& renderGraph;
    RenderResources& resources;
    DescriptorManager* descriptorManager;

    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> pipeline;

    ImGuiSystem* imguiSystem = nullptr;
};
