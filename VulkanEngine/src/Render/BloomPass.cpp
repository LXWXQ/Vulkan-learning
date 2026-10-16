#include "BloomPass.h"
#include "RHI/CommandUtils.h"
#include "AssetPaths.h"
#include <stdexcept>
#include <algorithm>
#include <array>

// push constant 的字节布局必须和 shaders/bloom_*.frag 里的声明逐一对应。
// GLSL 的 push_constant 块按 std430 布局：vec2 占 8 字节、对齐 8，
// 因此在 vec2 后面跟一个 int 时偏移是 8 而不是 12 —— C++ 这边的默认对齐
// 恰好一致，但"恰好一致"是靠不住的，用 static_assert 钉死。
static_assert(sizeof(BloomPass::BrightPush) == 12, "BrightPush 必须与 bloom_bright.frag 的 push_constant 等大");
static_assert(sizeof(BloomPass::DownPush) == 16, "DownPush 必须与 bloom_down.frag 的 push_constant 等大");
static_assert(sizeof(BloomPass::UpPush) == 16, "UpPush 必须与 bloom_up.frag 的 push_constant 等大");

namespace
{
    // 每级固定减半；分辨率低到 1 像素时就不再缩小，避免 0 尺寸图像
    VkExtent2D halfExtent(VkExtent2D e)
    {
        return VkExtent2D{ std::max(1u, e.width / 2u), std::max(1u, e.height / 2u) };
    }
}

BloomPass::BloomPass(Device& device, RenderGraph& graph, RenderResources& resources,
                     DescriptorManager* descMgr)
    : device(device), renderGraph(graph), resources(resources), descriptorManager(descMgr)
{
}

BloomPass::~BloomPass()
{
    if (brightLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.getDevice(), brightLayout, nullptr);
    if (downLayout != VK_NULL_HANDLE)   vkDestroyPipelineLayout(device.getDevice(), downLayout, nullptr);
    if (upLayout != VK_NULL_HANDLE)     vkDestroyPipelineLayout(device.getDevice(), upLayout, nullptr);
    if (setLayout != VK_NULL_HANDLE)    vkDestroyDescriptorSetLayout(device.getDevice(), setLayout, nullptr);
    if (sampler != VK_NULL_HANDLE)      vkDestroySampler(device.getDevice(), sampler, nullptr);
}

void BloomPass::init()
{
    createDescriptorLayout();
    createSampler();

    // ---- 金字塔各级：每级都是上一级的一半 ----
    // 各级同时要能当颜色附件（被写）和采样源（被读），所以 usage 两个都要。
    resources.bloomLevels.clear();
    resources.bloomLevels.reserve(kLevelCount);

    VkExtent2D extent = renderGraph.getExtent(resources.hdrScene);
    for (uint32_t i = 0; i < kLevelCount; ++i)
    {
        extent = halfExtent(extent);

        RenderGraph::TextureDesc desc;
        desc.name = "Bloom.Level" + std::to_string(i);
        desc.format = renderGraph.getFormat(resources.hdrScene);
        desc.extent = extent;
        desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        resources.bloomLevels.push_back(renderGraph.createTexture(desc));
    }
    // PresentPass 采样的就是最细的那一级
    resources.bloomResult = resources.bloomLevels.front();

    // 描述符集数量：1 个亮部提取 + (kLevelCount - 1) 次降采样 + (kLevelCount - 1) 次上采样。
    // 之所以能复用同一套 layout，是因为 Bloom 的每个 Pass 都只采样一张图。
    const uint32_t setCount = 1 + 2 * (kLevelCount - 1);
    sets.resize(setCount);
    for (uint32_t i = 0; i < setCount; ++i)
        sets[i] = descriptorManager->allocateSet(setLayout);

    // 纹理由渲染图持有，VkImageView 只在 start/end 时重建 —— 启动时写一次就够，
    // 不需要每帧 vkUpdateDescriptorSets。
    updateDescriptor(0, renderGraph.getView(resources.hdrScene));
    for (uint32_t i = 0; i < kLevelCount - 1; ++i)
    {
        updateDescriptor(1 + i, renderGraph.getView(resources.bloomLevels[i]));
        updateDescriptor(1 + (kLevelCount - 1) + i, renderGraph.getView(resources.bloomLevels[i + 1]));
    }

    createPipelines();
}

void BloomPass::createDescriptorLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.bindingCount = 1;
    info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &info, nullptr, &setLayout) != VK_SUCCESS)
        throw std::runtime_error("BloomPass: 描述符集布局创建失败");
}

void BloomPass::createSampler()
{
    // 金字塔各级都靠双线性抽样来"顺便"做一次平均，所以线性过滤是必须的
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1.0f;

    if (vkCreateSampler(device.getDevice(), &info, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("BloomPass: 采样器创建失败");
}

void BloomPass::updateDescriptor(uint32_t index, VkImageView view)
{
    VkDescriptorImageInfo imageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = sets[index];
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device.getDevice(), 1, &write, 0, nullptr);
}

void BloomPass::createPipelines()
{
    const VkFormat format = renderGraph.getFormat(resources.hdrScene);
    const std::string vert = SENTINEL_SHADER_DIR "fullscreen.vert.spv";

    auto makeLayout = [&](uint32_t pushSize)
    {
        VkPushConstantRange range{};
        range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        range.offset = 0;
        range.size = pushSize;

        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount = 1;
        info.pSetLayouts = &setLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(device.getDevice(), &info, nullptr, &layout) != VK_SUCCESS)
            throw std::runtime_error("BloomPass: 管线布局创建失败");
        return layout;
    };

    brightLayout = makeLayout(sizeof(BrightPush));
    downLayout   = makeLayout(sizeof(DownPush));
    upLayout     = makeLayout(sizeof(UpPush));

    auto baseConfig = [&]()
    {
        PipelineConfigInfo config{};
        Pipeline::defaultPipelineConfigInfo(config, WIDTH, HEIGHT);
        config.colorAttachmentFormats = { format };
        // 全屏三角形不需要顶点输入
        config.bindingDescriptions.clear();
        config.attributeDescriptions.clear();
        config.depthStencilInfo.depthTestEnable = VK_FALSE;
        config.depthStencilInfo.depthWriteEnable = VK_FALSE;
        return config;
    };

    {
        PipelineConfigInfo config = baseConfig();
        config.pipelineLayout = brightLayout;
        brightPipeline = std::make_unique<Pipeline>(device, vert,
            SENTINEL_SHADER_DIR "bloom_bright.frag.spv", config);
    }

    {
        PipelineConfigInfo config = baseConfig();
        config.pipelineLayout = downLayout;
        downPipeline = std::make_unique<Pipeline>(device, vert,
            SENTINEL_SHADER_DIR "bloom_down.frag.spv", config);
    }

    {
        PipelineConfigInfo config = baseConfig();
        config.pipelineLayout = upLayout;

        // 上采样是「把粗级累加回细级」，必须用加法混合，否则粗级会覆盖掉
        // 细级已经写好的内容 —— 辉光会变成一片模糊而没有细节。
        config.colorBlendAttachments[0].blendEnable = VK_TRUE;
        config.colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        config.colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        config.colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        config.colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        config.colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        config.colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        upPipeline = std::make_unique<Pipeline>(device, vert,
            SENTINEL_SHADER_DIR "bloom_up.frag.spv", config);
    }
}

void BloomPass::addToGraph(RenderGraph& graph, FrameInfo& frameInfo)
{
    const EngineSettings& s = frameInfo.settings;
    const uint32_t downs = kLevelCount - 1;

    // -----------------------------------------------------------------------
    // 【为什么"关掉 Bloom"不是跳过这几个 Pass】
    //
    // 直觉做法是 pass.setEnabled(s.bloomEnabled)，但那会破坏两条约束：
    //
    //   1) 语义：bloomLevels / bloomResult 是瞬时资源，每帧内容都会重置。
    //      跳过写入者之后，PresentPass 声明的读就变成"读未定义内容" ——
    //      RenderGraph::validateWriteCoverage 会直接报错（这个报错是对的）。
    //
    //   2) 更硬的一条，是验证层告诉我的：PresentPass 的描述符里绑着
    //      bloomResult，声明期望布局是 SHADER_READ_ONLY_OPTIMAL。
    //      **只要它绑在描述符上，这个布局转换就省不掉** —— 一旦没人读它，
    //      渲染图就不会为它发屏障，图像整帧停在 UNDEFINED，验证层直接刷
    //      "expects layout SHADER_READ_ONLY_OPTIMAL, current layout UNDEFINED"。
    //
    // 所以开关改成"强度置零"：Pass 照跑，亮部提取输出全黑，上采样把零累加上去。
    // bloomResult 因此永远是"这一帧写过的、布局正确的"图，
    // 而 PresentPass 用 bloomIntensity=0 把它的贡献抹掉，视觉结果等价于关闭。
    //
    // 代价：关掉 Bloom 时仍要跑 7 个全屏 Pass（都在半分辨率以下）。
    // 换来的是"声明与事实一致"，以及一条能被机器验证的不变量：
    //   每一个被 read 的瞬时资源，本帧一定有人写。
    // 后者比省下几趟全屏绘制值钱得多 —— 它是后面加 SSR / 自动曝光 / 颜色分级
    // 时唯一能拦住"关掉某个 Pass 就花屏"的东西。
    // -----------------------------------------------------------------------
    const bool active = s.bloomEnabled;

    VkClearValue clear{};
    clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    // ---- 1) 亮部提取：HDR -> 最细一级 ----
    {
        auto& pass = graph.addGraphicsPass("Bloom.Bright");
        pass.read(resources.hdrScene);
        pass.writeColor(resources.bloomLevels[0], VK_ATTACHMENT_LOAD_OP_CLEAR, clear);

        // 关闭时把强度置零 => 输出全黑 => 后续上采样累加的还是黑
        BrightPush push{ s.bloomThreshold, s.bloomKnee, active ? 1.0f : 0.0f };
        pass.execute([this, push](VkCommandBuffer cmd)
        {
            brightPipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brightLayout,
                0, 1, &sets[0], 0, nullptr);
            vkCmdPushConstants(cmd, brightLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    // ---- 2) 降采样链 ----
    // 首级做 Karis 加权：它直接吃 HDR，是萤火虫（孤立超亮像素）进来的唯一入口。
    for (uint32_t i = 0; i < downs; ++i)
    {
        auto& pass = graph.addGraphicsPass("Bloom.Down" + std::to_string(i));
        pass.read(resources.bloomLevels[i]);
        pass.writeColor(resources.bloomLevels[i + 1], VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        // 关闭时也照跑（见本函数开头对"为什么不用 setEnabled"的说明）

        const VkExtent2D srcExtent = graph.getExtent(resources.bloomLevels[i]);
        DownPush push{};
        push.srcTexelSize = glm::vec2(1.0f / float(srcExtent.width), 1.0f / float(srcExtent.height));
        push.karisAverage = (i == 0) ? 1 : 0;

        const uint32_t setIndex = 1 + i;
        pass.execute([this, push, setIndex](VkCommandBuffer cmd)
        {
            downPipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downLayout,
                0, 1, &sets[setIndex], 0, nullptr);
            vkCmdPushConstants(cmd, downLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    // ---- 3) 上采样叠加（由粗到细）----
    // LOAD_OP_LOAD + 加法混合 => 粗级结果累加进本级，不需要额外缓冲。
    for (uint32_t i = downs; i-- > 0; )
    {
        auto& pass = graph.addGraphicsPass("Bloom.Up" + std::to_string(i));
        pass.read(resources.bloomLevels[i + 1]);
        pass.writeColor(resources.bloomLevels[i], VK_ATTACHMENT_LOAD_OP_LOAD);
        // 关闭时也照跑：亮部提取已经输出全黑，这里累加的还是黑

        const VkExtent2D srcExtent = graph.getExtent(resources.bloomLevels[i + 1]);
        UpPush push{};
        push.srcTexelSize = glm::vec2(1.0f / float(srcExtent.width), 1.0f / float(srcExtent.height));
        push.radius = 1.0f;

        const uint32_t setIndex = 1 + downs + i;
        pass.execute([this, push, setIndex](VkCommandBuffer cmd)
        {
            upPipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upLayout,
                0, 1, &sets[setIndex], 0, nullptr);
            vkCmdPushConstants(cmd, upLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }
}
