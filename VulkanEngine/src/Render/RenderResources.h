#pragma once

#include "RHI/RenderGraph.h"
#include <vulkan/vulkan.h>
#include <vector>

// D-2：光照通道的输出格式（线性 HDR 中间目标）。
// 用 16F 而不是 11-11-10 或 R8：Bloom 要在 tonal 映射之前看到 >1 的高光，
// 而 10 位整数格式虽然也能表示 >1（在 UNORM 下不能，在 FLOAT 下能），
// 但 16F 的精度足够做多级降采样而不会在粗级积累出明显的阶梯。
inline constexpr VkFormat kHdrSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// 帧内渲染资源的集中登记处。
//
// 各 Pass 通过它交换产物：几何通道写 G-Buffer、SSAO 采样 G-Buffer、
// 光照通道写 HDR、Bloom 读写各级辉光、Present 读 HDR + Bloom 写交换链。
// 渲染图正是依靠这些「谁读了谁写了」的声明，才能自动推导出布局转换和屏障 ——
// 所以这个结构同时也是同步关系的唯一事实来源。
struct RenderResources
{
    // ---- G-Buffer（GeometryPass 写，SSAOPass / LightingPass 读）----
    RenderGraph::TextureHandle gbPosition;
    RenderGraph::TextureHandle gbNormal;
    RenderGraph::TextureHandle gbAlbedo;
    RenderGraph::TextureHandle gbPbr;
    RenderGraph::TextureHandle gbDepth;

    // ---- 中间产物 ----
    RenderGraph::TextureHandle ssao;       // SSAOPass 写，LightingPass 读

    // D-2：光照的终点不再是交换链，而是这张线性 HDR 图。
    // 它是后效链的输入 —— Bloom 在它上面提亮部，PresentPass 把它变成显示值。
    RenderGraph::TextureHandle hdrScene;   // LightingPass 写，BloomPass / PresentPass 读

    // ---- Bloom 金字塔（BloomPass 内部创建）----
    // 单独暴露出来是为了让 ImGui 能可视化某一级，也方便以后接自动曝光。
    std::vector<RenderGraph::TextureHandle> bloomLevels;
    RenderGraph::TextureHandle bloomResult;   // 供 PresentPass 采样的最终辉光图

    // PresentPass 用的采样器不属于渲染图（渲染图只管图像），由使用方持有
    VkSampler hdrSampler = VK_NULL_HANDLE;

    // ---- 跨帧常驻（Application 里导入一次，之后只读）----
    RenderGraph::TextureHandle environment;   // 等距柱状 HDR（天空盒）
    RenderGraph::TextureHandle irradiance;    // IBL 漫反射
    RenderGraph::TextureHandle prefiltered;   // IBL 镜面
    RenderGraph::TextureHandle brdfLut;       // IBL BRDF 积分表

    // ---- 交换链（每帧按 imageIndex 选一个）----
    std::vector<RenderGraph::TextureHandle> swapchainImages;
};
