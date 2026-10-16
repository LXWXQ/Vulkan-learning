#pragma once
#include "IRenderPass.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/RenderGraph.h"
#include "Render/RenderResources.h"
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

// 屏幕空间环境光遮蔽。
//
// D-1(b)：输出目标由渲染图创建（createTexture），自身不再持有 RenderPass / Framebuffer。
// D-2   ：输出格式保持 R8_UNORM —— 遮蔽因子是纯 0~1 的调制量，不需要 HDR。
//         它只调制光照里的环境项（见 lighting.frag 结尾），不参与 Bloom。
class SSAOPass : public IRenderPass
{
public:
    SSAOPass(Device& device, RenderGraph& graph, RenderResources& resources,
             VkDescriptorSetLayout globalLayout, DescriptorManager* descMgr);
    ~SSAOPass() override;

    void init() override;
    void addToGraph(RenderGraph& graph, FrameInfo& frameInfo) override;
    std::string getName() const override { return "SSAO"; }

    // 供 Application 写进全局描述符集（binding 6）
    VkSampler   getOutputSampler() const { return outputSampler; }
    VkImageView getOutputImageView() const;

private:
    void generateSampleKernel();
    void createNoiseTexture();
    void createUniformBuffer();
    void createLocalDescriptors();
    void createPipeline();

    Device& device;
    RenderGraph& renderGraph;
    RenderResources& resources;
    DescriptorManager* descriptorManager;
    VkDescriptorSetLayout globalSetLayout;

    std::vector<glm::vec4> ssaoKernel;

    struct
    {
        VkImage       image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView   view = VK_NULL_HANDLE;
        VkSampler     sampler = VK_NULL_HANDLE;
    } noiseTexture;

    VkBuffer      uboBuffer = VK_NULL_HANDLE;
    VmaAllocation uboAllocation = VK_NULL_HANDLE;
    void*         uboMapped = nullptr;

    VkSampler     outputSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout localSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       localSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> ssaoPipeline;
};