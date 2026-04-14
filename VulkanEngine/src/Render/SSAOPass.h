#pragma once
#include "Render/PostProcessPass.h"
#include "RHI/Pipeline.h" 
#include <glm/glm.hpp>
#include <vector>
#include <memory>

// SSAO UBO 数据结构
struct SSAOUbo {
    glm::mat4 projection;
    glm::mat4 view;
    glm::vec4 samples[64]; // 64 个半球采样点
    int kernelSize = 64;
    float radius = 0.5f;
    float bias = 0.025f;
    float padding; // 内存对齐
};

class SSAOPass : public PostProcessPass 
{
public:
    // 🌟 构造函数：不再需要 GBuffer 了！只要 globalLayout
    SSAOPass(Device& device, VkExtent2D extent, VkCommandPool commandPool, VkDescriptorSetLayout globalLayout);
    ~SSAOPass() override;

    void init() override;
    void execute(VkCommandBuffer commandBuffer, FrameInfo& frameInfo) override;

private:
    void generateSampleKernel();
    void createNoiseTexture();
    void createUniformBuffer();
    void createLocalDescriptors(); // 🌟 只创建私有的 Set 1
    void createPipeline();

    VkCommandPool commandPool;
    VkDescriptorSetLayout globalSetLayout; // 存下来，建管线时要用

    // SSAO 数据
    std::vector<glm::vec4> ssaoKernel;
    
    // 噪声贴图资源
    struct {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    } noiseTexture;

    // UBO 资源
    VkBuffer uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uboMemory = VK_NULL_HANDLE;
    void* uboMapped = nullptr;

    // 🌟 私有的局部描述符 (Set 1)
    VkDescriptorSetLayout localSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool localPool = VK_NULL_HANDLE;
    VkDescriptorSet localSet = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> ssaoPipeline;
};