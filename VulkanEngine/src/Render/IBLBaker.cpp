// ============================================================================
// IBLBaker：GPU 端一次性烘焙 diffuse irradiance / specular prefilter / BRDF LUT
//
// 为什么不用 CPU 烘：irradiance 需要 6*32*32 个纹素 × ~1.6 万次半球采样，
// prefilter 需要 6*128*128*~1.33 个纹素 × 1024 次 GGX 采样，CPU 上要几十秒。
//
// 全部烘焙在一条一次性命令缓冲里完成，中间用显式 barrier 串起来。
// 因为是一次性初始化，Framebuffer 采取"用完即建/即毁"的简单策略。
// ============================================================================

#include "Render/IBLBaker.h"
#include "RHI/CommandUtils.h"
#include "RHI/Texture.h"
#include "AssetPaths.h"

#include <stdexcept>
#include <iostream>
#include <array>

namespace
{
    constexpr VkPipelineStageFlags kColorOutStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    // 离屏单颜色附件 RenderPass。
    // 首尾布局都固定在 COLOR_ATTACHMENT_OPTIMAL —— 进出烘焙区的两次布局转换
    // 由 IBLBaker 自己用 barrier 做，避免在 RenderPass 里隐式转换导致同步不清。
    VkRenderPass createOffscreenRenderPass(VkDevice device, VkFormat format)
    {
        VkAttachmentDescription color{};
        color.format = format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = kColorOutStage;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dstStageMask = kColorOutStage;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = kColorOutStage;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &color;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(deps.size());
        info.pDependencies = deps.data();

        VkRenderPass pass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device, &info, nullptr, &pass) != VK_SUCCESS)
            throw std::runtime_error("IBLBaker: RenderPass 创建失败");
        return pass;
    }
}

IBLBaker::IBLBaker(Device& device, VkCommandPool commandPool)
    : device(device), commandPool(commandPool)
{
}

IBLBaker::~IBLBaker()
{
    // 兜底：正常流程下 bake() 已经清空了这个列表
    for (VkFramebuffer fb : pendingFramebuffers)
        vkDestroyFramebuffer(device.getDevice(), fb, nullptr);
    pendingFramebuffers.clear();

    equirectToCubePipeline.reset();
    irradiancePipeline.reset();
    prefilterPipeline.reset();
    brdfLutPipeline.reset();

    if (bakeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.getDevice(), bakeLayout, nullptr);
    if (lutLayout != VK_NULL_HANDLE)  vkDestroyPipelineLayout(device.getDevice(), lutLayout, nullptr);

    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device.getDevice(), descriptorPool, nullptr);
    if (srcSetLayout != VK_NULL_HANDLE)   vkDestroyDescriptorSetLayout(device.getDevice(), srcSetLayout, nullptr);

    if (cubeRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device.getDevice(), cubeRenderPass, nullptr);
    if (lutRenderPass != VK_NULL_HANDLE)  vkDestroyRenderPass(device.getDevice(), lutRenderPass, nullptr);

    if (brdfLutSampler != VK_NULL_HANDLE) vkDestroySampler(device.getDevice(), brdfLutSampler, nullptr);
    if (brdfLutView != VK_NULL_HANDLE)    vkDestroyImageView(device.getDevice(), brdfLutView, nullptr);
    if (brdfLutImage != VK_NULL_HANDLE)   vmaDestroyImage(device.getVmaAllocator(), brdfLutImage, brdfLutAllocation);
}

void IBLBaker::createTargets()
{
    envCube        = std::make_unique<CubemapTexture>(device, kEnvCubeSize, kEnvCubeMips, kCubeFormat, "envCube");
    irradianceMap  = std::make_unique<CubemapTexture>(device, kIrradianceSize, 1, kCubeFormat, "irradiance");
    prefilteredMap = std::make_unique<CubemapTexture>(device, kPrefilterSize, kPrefilterMips, kCubeFormat, "prefiltered");

    // BRDF LUT：2D RG16F，既要当渲染目标也要能被采样
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kLutFormat;
    info.extent = { kBrdfLutSize, kBrdfLutSize, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(device.getVmaAllocator(), &info, &alloc, &brdfLutImage, &brdfLutAllocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: BRDF LUT 图像创建失败");

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = brdfLutImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = kLutFormat;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device.getDevice(), &view, nullptr, &brdfLutView) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: BRDF LUT view 创建失败");

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f;
    if (vkCreateSampler(device.getDevice(), &si, nullptr, &brdfLutSampler) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: BRDF LUT 采样器创建失败");

    std::cout << "[IBL] 目标已创建: envCube " << kEnvCubeSize << "(mips " << kEnvCubeMips
              << ")  irradiance " << kIrradianceSize
              << "  prefiltered " << kPrefilterSize << "(mips " << kPrefilterMips
              << ")  brdfLut " << kBrdfLutSize << "\n";
}

void IBLBaker::createRenderPasses()
{
    // D-1(b) 起改用 Vulkan 1.3 动态渲染：烘焙不再需要 VkRenderPass / VkFramebuffer。
    // 保留成空函数是为了不动 bake() 里的调用顺序。
}

void IBLBaker::createDescriptors(Texture& equirectEnv)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr, &srcSetLayout) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: 描述符布局创建失败");

    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: 描述符池创建失败");

    VkDescriptorSetLayout layouts[2] = { srcSetLayout, srcSetLayout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    if (vkAllocateDescriptorSets(device.getDevice(), &allocInfo, sets) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: 描述符集分配失败");
    equirectSet = sets[0];
    envCubeSet  = sets[1];

    VkDescriptorImageInfo equirectInfo{ equirectEnv.getSampler(), equirectEnv.getView(),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo cubeInfo{ envCube->getSampler(), envCube->getView(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = equirectSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &equirectInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = envCubeSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &cubeInfo;

    vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
void IBLBaker::createPipelines()
{
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount = 1;
    li.pSetLayouts = &srcSetLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(device.getDevice(), &li, nullptr, &bakeLayout) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: bake 管线布局创建失败");

    VkPipelineLayoutCreateInfo li2{};
    li2.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li2.setLayoutCount = 0;
    li2.pushConstantRangeCount = 0;
    if (vkCreatePipelineLayout(device.getDevice(), &li2, nullptr, &lutLayout) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: LUT 管线布局创建失败");

    const std::string vert = SENTINEL_SHADER_DIR "ibl_capture.vert.spv";

    PipelineConfigInfo cfg{};
    Pipeline::defaultPipelineConfigInfo(cfg, kEnvCubeSize, kEnvCubeSize);
    cfg.colorAttachmentFormats = { kCubeFormat };   // 动态渲染：声明附件格式即可
    cfg.pipelineLayout = bakeLayout;
    // 全屏三角形不需要顶点输入
    cfg.bindingDescriptions.clear();
    cfg.attributeDescriptions.clear();
    cfg.depthStencilInfo.depthTestEnable = VK_FALSE;
    cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;

    equirectToCubePipeline = std::make_unique<Pipeline>(device, vert,
        SENTINEL_SHADER_DIR "ibl_equirect_to_cube.frag.spv", cfg);
    irradiancePipeline = std::make_unique<Pipeline>(device, vert,
        SENTINEL_SHADER_DIR "ibl_irradiance.frag.spv", cfg);
    prefilterPipeline = std::make_unique<Pipeline>(device, vert,
        SENTINEL_SHADER_DIR "ibl_prefilter.frag.spv", cfg);

    PipelineConfigInfo lutCfg = cfg;
    lutCfg.colorAttachmentFormats = { kLutFormat };
    lutCfg.pipelineLayout = lutLayout;
    brdfLutPipeline = std::make_unique<Pipeline>(device, vert,
        SENTINEL_SHADER_DIR "ibl_brdf_lut.frag.spv", lutCfg);

    std::cout << "[IBL] 4 条烘焙管线已就绪\n";
}

VkFramebuffer IBLBaker::makeFramebuffer(VkRenderPass pass, VkImageView view, uint32_t size)
{
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = pass;
    info.attachmentCount = 1;
    info.pAttachments = &view;
    info.width = size;
    info.height = size;
    info.layers = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device.getDevice(), &info, nullptr, &fb) != VK_SUCCESS)
        throw std::runtime_error("IBLBaker: Framebuffer 创建失败");

    pendingFramebuffers.push_back(fb);
    return fb;
}

void IBLBaker::transitionImage(VkCommandBuffer cmd, VkImage image, uint32_t mipLevels, uint32_t layerCount,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    transitionImageRange(cmd, image, 0, mipLevels, layerCount, oldLayout, newLayout,
                         srcAccess, dstAccess, srcStage, dstStage);
}

void IBLBaker::transitionImageRange(VkCommandBuffer cmd, VkImage image, uint32_t baseMip, uint32_t mipCount,
                                    uint32_t layerCount,
                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                    VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, layerCount };
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void IBLBaker::renderFaces(VkCommandBuffer cmd, CubemapTexture& target, Pipeline* pipeline,
                           VkPipelineLayout layout, VkDescriptorSet set,
                           int32_t sampleCount, float sourceFaceSize, bool perMipRoughness)
{
    const uint32_t mipCount = target.getMipLevels();

    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t size = target.getFaceSizeAtMip(mip);
        const float roughness = (perMipRoughness && mipCount > 1)
            ? static_cast<float>(mip) / static_cast<float>(mipCount - 1)
            : 0.0f;

        for (uint32_t face = 0; face < 6; ++face)
        {
            // 动态渲染：直接对着这一面的 imageView 开渲染，不需要 Framebuffer
            VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            color.imageView = target.getFaceView(mip, face);
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

            VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            info.renderArea.offset = { 0, 0 };
            info.renderArea.extent = { size, size };
            info.layerCount = 1;
            info.colorAttachmentCount = 1;
            info.pColorAttachments = &color;

            vkCmdBeginRendering(cmd, &info);

            cmdSetFullViewportAndScissor(cmd, VkExtent2D{ size, size });
            pipeline->bind(cmd);
            if (set != VK_NULL_HANDLE)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);

            PushConstants pc{};
            pc.faceIndex = static_cast<int32_t>(face);
            pc.roughness = roughness;
            pc.sampleCount = sampleCount;
            pc.sourceFaceSize = sourceFaceSize;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);
        }
    }
}

void IBLBaker::renderBrdfLut(VkCommandBuffer cmd)
{
    // 动态渲染：直接对着 LUT 的 imageView 开渲染
    VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color.imageView = brdfLutView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = { kBrdfLutSize, kBrdfLutSize };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &info);
    cmdSetFullViewportAndScissor(cmd, VkExtent2D{ kBrdfLutSize, kBrdfLutSize });
    brdfLutPipeline->bind(cmd);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

void IBLBaker::recordEquirectToCube(VkCommandBuffer cmd)
{
    renderFaces(cmd, *envCube, equirectToCubePipeline.get(), bakeLayout, equirectSet,
                0, static_cast<float>(kEnvCubeSize), false);
}

void IBLBaker::recordEnvCubeMips(VkCommandBuffer cmd)
{
    // 用 vkCmdBlitImage 逐面生成 mip 链。
    // prefilter 着色器会按"采样立体角 / 源纹素立体角"来 textureLod，
    // 没有这条链的话高粗糙度会严重闪烁。
    for (uint32_t mip = 1; mip < envCube->getMipLevels(); ++mip)
    {
        const int32_t srcSize = static_cast<int32_t>(envCube->getFaceSizeAtMip(mip - 1));
        const int32_t dstSize = static_cast<int32_t>(envCube->getFaceSizeAtMip(mip));

        for (uint32_t face = 0; face < 6; ++face)
        {
            VkImageBlit region{};
            region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, face, 1 };
            region.srcOffsets[0] = { 0, 0, 0 };
            region.srcOffsets[1] = { srcSize, srcSize, 1 };
            region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 };
            region.dstOffsets[0] = { 0, 0, 0 };
            region.dstOffsets[1] = { dstSize, dstSize, 1 };

            vkCmdBlitImage(cmd, envCube->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           envCube->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region, VK_FILTER_LINEAR);
        }

        // 刚写入的这一级要充当下一级的「源」，所以从 DST 转成 SRC。
        // 这里刻意逐 mip 指定精确布局（而不是整图 GENERAL）——
        // GENERAL 虽然功能正确，但验证层会对每次 blit 刷性能警告，把真错误淹掉。
        transitionImageRange(cmd, envCube->getImage(), mip, 1, 6,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
}

void IBLBaker::recordIrradiance(VkCommandBuffer cmd)
{
    // irradiance 的采样数由着色器内固定的 sampleDelta 决定，push 里传 0 即可
    renderFaces(cmd, *irradianceMap, irradiancePipeline.get(), bakeLayout, envCubeSet,
                0, static_cast<float>(kEnvCubeSize), false);
}

void IBLBaker::recordPrefilter(VkCommandBuffer cmd)
{
    renderFaces(cmd, *prefilteredMap, prefilterPipeline.get(), bakeLayout, envCubeSet,
                kPrefilterSamples, static_cast<float>(kEnvCubeSize), true);
}

void IBLBaker::bake(Texture& equirectEnv)
{
    createTargets();
    createRenderPasses();
    createDescriptors(equirectEnv);
    createPipelines();

    VkCommandBuffer cmd = cmdBeginSingleTime(device.getDevice(), commandPool);

    const VkPipelineStageFlags top = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkPipelineStageFlags colorOut = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags transfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkPipelineStageFlags frag = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    // 所有目标统一 UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL（内容由 clear 负责）
    transitionImage(cmd, envCube->getImage(), kEnvCubeMips, 6, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, top, colorOut);
    transitionImage(cmd, irradianceMap->getImage(), 1, 6, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, top, colorOut);
    transitionImage(cmd, prefilteredMap->getImage(), kPrefilterMips, 6, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, top, colorOut);
    transitionImage(cmd, brdfLutImage, 1, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, top, colorOut);

    // 1) 等距柱状 -> 立方体
    recordEquirectToCube(cmd);

    // 2) 生成 envCube 的 mip 链。
    //    mip0 当「源」，其余 mip 当「目标」，布局分开设置。
    transitionImageRange(cmd, envCube->getImage(), 0, 1, 6,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, colorOut, transfer);
    transitionImageRange(cmd, envCube->getImage(), 1, kEnvCubeMips - 1, 6,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, colorOut, transfer);
    recordEnvCubeMips(cmd);

    // 跑完之后所有 mip 都是 TRANSFER_SRC，统一转只读作为采样源
    transitionImage(cmd, envCube->getImage(), kEnvCubeMips, 6,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, transfer, frag);

    // 3) 漫反射 IBL
    recordIrradiance(cmd);

    // 4) 镜面 IBL（GGX 重要性采样 + mip 链）
    recordPrefilter(cmd);

    // 5) BRDF 积分表
    renderBrdfLut(cmd);

    // 收尾：三个烘焙产物都转为只读，供光照阶段采样
    transitionImage(cmd, irradianceMap->getImage(), 1, 6,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, colorOut, frag);
    transitionImage(cmd, prefilteredMap->getImage(), kPrefilterMips, 6,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, colorOut, frag);
    transitionImage(cmd, brdfLutImage, 1, 1,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, colorOut, frag);

    cmdEndSingleTime(device.getDevice(), device.getGraphicsQueue(), commandPool, cmd);

    // 到这里 GPU 已经执行完毕（cmdEndSingleTime 内部 vkQueueWaitIdle），可以安全释放
    for (VkFramebuffer fb : pendingFramebuffers)
        vkDestroyFramebuffer(device.getDevice(), fb, nullptr);
    pendingFramebuffers.clear();

    std::cout << "[IBL] 烘焙完成（irradiance / prefiltered / brdfLut）\n";
}