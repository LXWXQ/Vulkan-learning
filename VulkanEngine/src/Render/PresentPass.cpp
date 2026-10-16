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
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
    if (setLayout != VK_NULL_HANDLE)      vkDestroyDescriptorSetLayout(device.getDevice(), setLayout, nullptr);
    if (sampler != VK_NULL_HANDLE)        vkDestroySampler(device.getDevice(), sampler, nullptr);
}

void PresentPass::init()
{
    createDescriptorLayout();

    // 两张输入图都是全屏分辨率级别的，用线性 + CLAMP_TO_EDGE 即可
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

    set = descriptorManager->allocateSet(setLayout);

    // 两张输入纹理都由渲染图持有，VkImageView 在运行期不会重建，写一次即可。
    // 注意 Bloom 关闭时 bloomResult 仍然是一张合法图像（只是内容是 0），
    // 所以这里不需要为了"关闭状态"准备别的描述符集 —— 关闭与否由 push constant 决定。
    VkDescriptorImageInfo infos[2] = {
        { sampler, renderGraph.getView(resources.hdrScene), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { sampler, renderGraph.getView(resources.bloomResult), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };

    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device.getDevice(), 2, writes, 0, nullptr);

    // 光照通道之外的人（例如以后接自动曝光）可以复用这个采样器去读 HDR 中间目标
    resources.hdrSampler = sampler;

    createPipeline();
}

void PresentPass::createDescriptorLayout()
{
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.bindingCount = 2;
    info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &info, nullptr, &setLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: 描述符集布局创建失败");
}

void PresentPass::createPipeline()
{
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PresentPush);

    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;

    if (vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("PresentPass: 管线布局创建失败");

    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config, WIDTH, HEIGHT);
    // 输出格式必须和交换链实际格式一致 —— 硬件只认这一个
    config.colorAttachmentFormats = { swapchain.getSwapChainImageFormat() };
    config.pipelineLayout = pipelineLayout;
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    pipeline = std::make_unique<Pipeline>(device,
        SENTINEL_SHADER_DIR "fullscreen.vert.spv",
        SENTINEL_SHADER_DIR "present.frag.spv", config);
}

void PresentPass::addToGraph(RenderGraph& graph, FrameInfo& frameInfo)
{
    auto& pass = graph.addGraphicsPass("Present");

    pass.read(resources.hdrScene);

    // bloomResult 必须无条件声明读 —— 不能因为"Bloom 关了"就省掉。
    //
    // 这里踩过一次，值得写下来：这张图绑在 PresentPass 的描述符里，声明期望
    // 布局是 SHADER_READ_ONLY_OPTIMAL。**只要它绑在描述符上，这个布局转换就
    // 省不掉**：一旦没人声明读它，渲染图就不会为它发屏障，图像整帧停在
    // UNDEFINED，验证层会直接刷
    //   "expects layout SHADER_READ_ONLY_OPTIMAL, current layout UNDEFINED"。
    //
    // 所以"关闭 Bloom"不是靠这里不读，而是靠 BloomPass 把亮部提取的强度
    // 置零（输出全黑）+ 这里 bloomIntensity 传 0。两张图每帧都有人写、
    // 布局永远正确，语义校验也就永远成立。
    pass.read(resources.bloomResult);

    // 本帧的交换链图像。loadOp 用 DONT_CARE：整屏都会被全屏三角形覆盖，
    // 读回旧内容是纯粹的带宽浪费。
    pass.writeColor(resources.swapchainImages[frameInfo.frameIndex],
        VK_ATTACHMENT_LOAD_OP_DONT_CARE);

    PresentPush push{};
    push.exposure = frameInfo.settings.exposure;
    push.bloomIntensity = frameInfo.settings.bloomIntensity;
    push.bloomEnabled = frameInfo.settings.bloomEnabled ? 1.0f : 0.0f;

    pass.execute([this, push](VkCommandBuffer cmd)
    {
        pipeline->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        // ImGui 叠加在最终图像之上：它必须在动态渲染作用域内绘制。
        // 由于所有图形管线都启用了动态 viewport/scissor，ImGui 后端会自己
        // 调用 vkCmdSetViewport/vkCmdSetScissor（见 imgui_impl_vulkan.cpp:505），
        // 所以这里不需要再补设置 —— 但顺序上必须在 vkCmdDraw 之后。
        if (imguiSystem != nullptr)
            imguiSystem->renderDrawData(cmd);
    });
}
