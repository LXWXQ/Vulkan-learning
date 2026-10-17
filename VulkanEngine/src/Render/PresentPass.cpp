#include "PresentPass.h"
#include "AssetPaths.h"
#include <stdexcept>

PresentPass::PresentPass(Device& device, Swapchain& swapchain, RenderGraph& graph,
                         RenderResources& resources, DescriptorManager* descMgr)
    : device(device), swapchain(swapchain), renderGraph(graph),
      resources(resources), descriptorManager(descMgr)
{
}

PresentPass::~PresentPass()
{
    pipeline.reset();
    noBloomPipeline.reset();
    if (pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
    if (noBloomPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device.getDevice(), noBloomPipelineLayout, nullptr);
    if (setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device.getDevice(), setLayout, nullptr);
    if (noBloomSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device.getDevice(), noBloomSetLayout, nullptr);
    if (sampler != VK_NULL_HANDLE)
        vkDestroySampler(device.getDevice(), sampler, nullptr);
}

void PresentPass::init()
{
    createDescriptorLayout();

    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f;
    if (vkCreateSampler(device.getDevice(), &si, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: 采样器创建失败");

    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        sets[frame] = descriptorManager->allocateSet(setLayout);
        noBloomSets[frame] = descriptorManager->allocateSet(noBloomSetLayout);
        updateBloomDescriptor(frame);
        updateNoBloomDescriptor(frame);
    }

    resources.hdrSampler = sampler;
    createPipeline();
}

void PresentPass::updateBloomDescriptor(uint32_t frame)
{
    VkDescriptorImageInfo infos[2] = {
        { sampler, renderGraph.getView(resources.hdrScene[frame]), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { sampler, renderGraph.getView(resources.bloomResult[frame]), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };

    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = sets[frame];
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device.getDevice(), 2, writes, 0, nullptr);
}

void PresentPass::updateNoBloomDescriptor(uint32_t frame)
{
    VkDescriptorImageInfo hdrInfo{
        sampler, renderGraph.getView(resources.hdrScene[frame]),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = noBloomSets[frame];
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &hdrInfo;
    vkUpdateDescriptorSets(device.getDevice(), 1, &write, 0, nullptr);
}

void PresentPass::createDescriptorLayout()
{
    VkDescriptorSetLayoutBinding bloomBindings[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        bloomBindings[i].binding = i;
        bloomBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bloomBindings[i].descriptorCount = 1;
        bloomBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo bloomInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    bloomInfo.bindingCount = 2;
    bloomInfo.pBindings = bloomBindings;
    if (vkCreateDescriptorSetLayout(device.getDevice(), &bloomInfo, nullptr, &setLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: Bloom 描述符集布局创建失败");

    VkDescriptorSetLayoutBinding noBloomBinding{};
    noBloomBinding.binding = 0;
    noBloomBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    noBloomBinding.descriptorCount = 1;
    noBloomBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo noBloomInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    noBloomInfo.bindingCount = 1;
    noBloomInfo.pBindings = &noBloomBinding;
    if (vkCreateDescriptorSetLayout(device.getDevice(), &noBloomInfo, nullptr, &noBloomSetLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: NoBloom 描述符集布局创建失败");
}

void PresentPass::createPipeline()
{
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PresentPush);

    VkPipelineLayoutCreateInfo bloomLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    bloomLayoutInfo.setLayoutCount = 1;
    bloomLayoutInfo.pSetLayouts = &setLayout;
    bloomLayoutInfo.pushConstantRangeCount = 1;
    bloomLayoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(device.getDevice(), &bloomLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: Bloom 管线布局创建失败");

    VkPipelineLayoutCreateInfo noBloomLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    noBloomLayoutInfo.setLayoutCount = 1;
    noBloomLayoutInfo.pSetLayouts = &noBloomSetLayout;
    noBloomLayoutInfo.pushConstantRangeCount = 1;
    noBloomLayoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(device.getDevice(), &noBloomLayoutInfo, nullptr, &noBloomPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: NoBloom 管线布局创建失败");

    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config, WIDTH, HEIGHT);
    config.colorAttachmentFormats = { swapchain.getSwapChainImageFormat() };
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    config.pipelineLayout = pipelineLayout;
    pipeline = std::make_unique<Pipeline>(device,
        SENTINEL_SHADER_DIR "fullscreen.vert.spv",
        SENTINEL_SHADER_DIR "present.frag.spv", config);

    config.pipelineLayout = noBloomPipelineLayout;
    noBloomPipeline = std::make_unique<Pipeline>(device,
        SENTINEL_SHADER_DIR "fullscreen.vert.spv",
        SENTINEL_SHADER_DIR "present_no_bloom.frag.spv", config);

    pipelineSwapchainFormat = swapchain.getSwapChainImageFormat();
}

void PresentPass::resize(VkExtent2D /*extent*/)
{
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        updateBloomDescriptor(frame);
        updateNoBloomDescriptor(frame);
    }

    if (pipelineSwapchainFormat != swapchain.getSwapChainImageFormat())
    {
        pipeline.reset();
        noBloomPipeline.reset();
        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (noBloomPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device.getDevice(), noBloomPipelineLayout, nullptr);
            noBloomPipelineLayout = VK_NULL_HANDLE;
        }
        createPipeline();
    }
}

void PresentPass::addToGraph(RenderGraph& graph, FrameInfo& frameInfo)
{
    const uint32_t frame = frameInfo.frameInFlightIndex;
    const bool bloomEnabled = frameInfo.settings.bloomEnabled;
    auto& pass = graph.addGraphicsPass("Present");

    pass.read(resources.hdrScene[frame]);
    if (bloomEnabled)
        pass.read(resources.bloomResult[frame]);

    pass.writeColor(resources.swapchainImages[frameInfo.swapchainImageIndex],
        VK_ATTACHMENT_LOAD_OP_DONT_CARE);

    PresentPush push{};
    push.exposure = frameInfo.settings.exposure;
    push.bloomIntensity = bloomEnabled ? frameInfo.settings.bloomIntensity : 0.0f;
    push.bloomEnabled = bloomEnabled ? 1.0f : 0.0f;

    pass.execute([this, push, frame, bloomEnabled](VkCommandBuffer cmd)
    {
        if (bloomEnabled)
        {
            pipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                0, 1, &sets[frame], 0, nullptr);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(push), &push);
        }
        else
        {
            noBloomPipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, noBloomPipelineLayout,
                0, 1, &noBloomSets[frame], 0, nullptr);
            vkCmdPushConstants(cmd, noBloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(push), &push);
        }

        vkCmdDraw(cmd, 3, 1, 0, 0);

        if (imguiSystem != nullptr)
            imguiSystem->renderDrawData(cmd);
    });
}