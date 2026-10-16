#include "GeometryPass.h"
#include "Core/DescriptorManager.h"
#include <iostream>

GeometryPass::GeometryPass(Device& device, GBuffer& gBuffer, RenderGraph& graph,
                           RenderResources& resources,
                           VkDescriptorSetLayout globalSetLayout,
                           VkDescriptorSetLayout materialSetLayout)
    : vulkanDevice(device), gBuffer(gBuffer), renderGraph(graph), resources(resources),
      globalSetLayout(globalSetLayout), materialSetLayout(materialSetLayout)
{
}

GeometryPass::~GeometryPass() = default;

void GeometryPass::init()
{
    // 把 G-Buffer 的 5 张附件导入渲染图。
    // initialLayout 用 UNDEFINED：它们每帧都会被完整重写，内容不需要保留。
    auto import = [&](const char* name, const FrameBufferAttachment& att, VkImageUsageFlags usage)
    {
        RenderGraph::TextureDesc desc;
        desc.name = name;
        desc.format = att.format;
        desc.extent = { WIDTH, HEIGHT };
        desc.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
        return renderGraph.importTexture(desc, att.image, att.view, VK_IMAGE_LAYOUT_UNDEFINED);
    };

    const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    resources.gbPosition = import("GBuffer.Position", gBuffer.getPosition(), colorUsage);
    resources.gbNormal   = import("GBuffer.Normal",   gBuffer.getNormal(),   colorUsage);
    resources.gbAlbedo   = import("GBuffer.Albedo",   gBuffer.getAlbedo(),   colorUsage);
    resources.gbPbr      = import("GBuffer.Pbr",      gBuffer.getPbr(),      colorUsage);
    resources.gbDepth    = import("GBuffer.Depth",    gBuffer.getDepth(),    depthUsage);

    geometrySystem = std::make_unique<GeometrySystem>(vulkanDevice, gBuffer.getDepth().format,
                                                      globalSetLayout, materialSetLayout);
}

void GeometryPass::addToGraph(RenderGraph& graph, FrameInfo& frameInfo)
{
    VkClearValue clearColor{};
    clearColor.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkClearValue clearDepth{};
    clearDepth.depthStencil = { 1.0f, 0 };

    auto& pass = graph.addGraphicsPass("Geometry");
    pass.writeColor(resources.gbPosition, VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbNormal,   VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbAlbedo,   VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeColor(resources.gbPbr,      VK_ATTACHMENT_LOAD_OP_CLEAR, clearColor);
    pass.writeDepth(resources.gbDepth,    VK_ATTACHMENT_LOAD_OP_CLEAR, clearDepth);

    pass.execute([this, &frameInfo](VkCommandBuffer cmd)
    {
        geometrySystem->render(cmd, frameInfo);
    });
}