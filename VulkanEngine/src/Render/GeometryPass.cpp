#include "GeometryPass.h"
#include "Core/DescriptorManager.h"
#include <iostream>

GeometryPass::GeometryPass(Device& device, const std::array<GBuffer*, kFramesInFlight>& gBuffers,
                           RenderGraph& graph, RenderResources& resources,
                           VkDescriptorSetLayout globalSetLayout,
                           VkDescriptorSetLayout materialSetLayout)
    : vulkanDevice(device), gBuffers(gBuffers), renderGraph(graph), resources(resources),
      globalSetLayout(globalSetLayout), materialSetLayout(materialSetLayout)
{
}

GeometryPass::~GeometryPass() = default;

void GeometryPass::init()
{
    // D-4：每帧槽位导入自己的一套 G-Buffer。
    // initialLayout 用 UNDEFINED：它们每帧都会被完整重写，内容不需要保留。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        GBuffer& gBuffer = *gBuffers[frame];
        auto import = [&](const char* name, const FrameBufferAttachment& att, VkImageUsageFlags usage)
        {
            RenderGraph::TextureDesc desc;
            desc.name = std::string(name) + ".Frame" + std::to_string(frame);
            desc.format = att.format;
            desc.extent = gBuffer.getExtent();
            desc.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
            return renderGraph.importTexture(desc, att.image, att.view, VK_IMAGE_LAYOUT_UNDEFINED);
        };

        const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        resources.gbPosition[frame] = import("GBuffer.Position", gBuffer.getPosition(), colorUsage);
        resources.gbNormal[frame]   = import("GBuffer.Normal",   gBuffer.getNormal(),   colorUsage);
        resources.gbAlbedo[frame]   = import("GBuffer.Albedo",   gBuffer.getAlbedo(),   colorUsage);
        resources.gbPbr[frame]      = import("GBuffer.Pbr",      gBuffer.getPbr(),      colorUsage);
        resources.gbDepth[frame]    = import("GBuffer.Depth",    gBuffer.getDepth(),    depthUsage);
    }

    // 所有 G-Buffer 的格式相同，几何管线只建一份。
    geometrySystem = std::make_unique<GeometrySystem>(vulkanDevice, gBuffers[0]->getDepth().format,
                                                      globalSetLayout, materialSetLayout);
}

void GeometryPass::addToGraph(RenderGraph& graph, FrameInfo& frameInfo)
{
    const uint32_t frame = frameInfo.frameInFlightIndex;

    VkClearValue clearColor{};
    clearColor.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkClearValue clearDepth{};
    clearDepth.depthStencil = { 1.0f, 0 };

    auto& pass = graph.addGraphicsPass("Geometry");
    pass.writeColor(resources.gbPosition[frame], VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbNormal[frame],   VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbAlbedo[frame],   VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbPbr[frame],      VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeDepth(resources.gbDepth[frame],    VK_ATTACHMENT_LOAD_OP_CLEAR, clearDepth);

    pass.execute([this, &frameInfo](VkCommandBuffer cmd)
    {
        geometrySystem->render(cmd, frameInfo);
    });
}
