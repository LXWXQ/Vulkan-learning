#include "RHI/RenderGraph.h"
#include "RHI/CommandUtils.h"

#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace
{
    // 「写」访问位。用于判断是否需要屏障：写->任何 都是 hazard。
    constexpr VkAccessFlags2 kWriteBits =
        VK_ACCESS_2_SHADER_WRITE_BIT |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_TRANSFER_WRITE_BIT |
        VK_ACCESS_2_HOST_WRITE_BIT;

    bool isDepthFormat(VkFormat f)
    {
        switch (f)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    VkImageAspectFlags aspectOf(bool isDepth)
    {
        return isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// ============================================================================
// RenderGraph
// ============================================================================

RenderGraph::RenderGraph(Device& device) : device(device) {}

RenderGraph::~RenderGraph()
{
    for (TextureEntry& e : textures)
    {
        if (!e.owned) continue;   // 导入的资源由调用方负责销毁
        if (e.view != VK_NULL_HANDLE) vkDestroyImageView(device.getDevice(), e.view, nullptr);
        if (e.image != VK_NULL_HANDLE) vmaDestroyImage(device.getVmaAllocator(), e.image, e.allocation);
    }
}

void RenderGraph::beginFrame(VkCommandBuffer commandBuffer)
{
    cmd = commandBuffer;

    for (std::unique_ptr<Pass>& p : passes)
        p.reset();
    passes.clear();

    // 每帧复位「瞬时资源」的布局记录。
    // importTexture 时如果给了非 UNDEFINED 的初始布局，说明它是跨帧常驻的只读资源
    // （例如 IBL cubemap），这种不能复位 —— 否则会被当成「内容未定义」而丢弃。
    for (TextureEntry& e : textures)
    {
        if (!e.resetEachFrame) continue;
        e.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        e.stage = VK_PIPELINE_STAGE_2_NONE;
        e.access = VK_ACCESS_2_NONE;
    }
}

void RenderGraph::endFrame()
{
    if (cmd == VK_NULL_HANDLE)
        throw std::runtime_error("RenderGraph: 调用 endFrame 之前必须先 beginFrame");

    // ---- 语义校验：先查"谁能读到没人写的东西"，再开始录制 ----
    // 放在最前面，是为了让错误在"这一帧的 GPU 命令还没提交"时就抛出来，
    // 而不是让画面先花一帧。
    validateWriteCoverage();

    for (std::unique_ptr<Pass>& passPtr : passes)
    {
        Pass& pass = *passPtr;
        if (!pass.enabled) continue;

        // ---- 0) 同一个 Pass 里对同一张图可能声明了多次访问 ----
        // 典型是 Bloom 的上采样：它既要把粗级结果当纹理采样，
        // 又要把本级当颜色附件累加写入。按声明顺序插入的屏障会变成
        // "转到 SHADER_READ、再转回 COLOR_ATTACHMENT"，白白多发一次。
        // 只保留最后一次声明即可 —— 屏障本来就只需要保证
        // "进入这个 Pass 时资源处于正确的布局"。
        std::vector<Access> accesses;
        accesses.reserve(pass.accesses.size());
        for (const Access& acc : pass.accesses)
        {
            if (!acc.texture.valid()) continue;
            auto it = std::find_if(accesses.begin(), accesses.end(),
                [&](const Access& a) { return a.texture.id == acc.texture.id; });
            if (it != accesses.end())
                *it = acc;
            else
                accesses.push_back(acc);
        }

        // ---- 0.5) 声明合法性校验 ----
        // 渲染图的核心承诺是「声明即同步」，代价是漏声明不会报错，只会出
        // 屏障缺失或 usage 不匹配的怪画面。这里把最常见的两类错误在
        // 录制前就挑出来：句柄越界、以及拿没有对应 usage 的图当附件用。
        for (const Access& acc : accesses)
        {
            if (acc.texture.id >= textures.size())
            {
                throw std::runtime_error("RenderGraph: Pass \"" + pass.name +
                    "\" 引用了越界的纹理句柄");
            }

            const TextureEntry& te = textures[acc.texture.id];
            const uint32_t id = acc.texture.id;

            const bool usedAsColor = std::any_of(pass.colors.begin(), pass.colors.end(),
                [&](const ColorAttachment& c) { return c.texture.id == id; });
            const bool usedAsDepth = std::any_of(pass.depths.begin(), pass.depths.end(),
                [&](const DepthAttachment& d) { return d.texture.id == id; });

            if (usedAsColor && !(te.desc.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
                throw std::runtime_error("RenderGraph: 纹理 \"" + te.desc.name +
                    "\" 被当作颜色附件，但创建时没有声明 COLOR_ATTACHMENT usage");
            if (usedAsDepth && !(te.desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
                throw std::runtime_error("RenderGraph: 纹理 \"" + te.desc.name +
                    "\" 被当作深度附件，但创建时没有声明 DEPTH_STENCIL_ATTACHMENT usage");
            if ((acc.access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) &&
                !(te.desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT))
                throw std::runtime_error("RenderGraph: 纹理 \"" + te.desc.name +
                    "\" 被采样，但创建时没有声明 SAMPLED usage");
            if ((acc.access & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) &&
                !(te.desc.usage & VK_IMAGE_USAGE_STORAGE_BIT))
                throw std::runtime_error("RenderGraph: 纹理 \"" + te.desc.name +
                    "\" 被当作存储图像，但创建时没有声明 STORAGE usage");
        }

        // ---- 1) 资源屏障：由声明自动推导 ----
        for (const Access& acc : accesses)
            emitBarrierIfNeeded(acc);

        // ---- 2) 动态渲染 ----
        const bool raster = !pass.colors.empty() || !pass.depths.empty();

        if (raster)
        {
            std::vector<VkRenderingAttachmentInfo> colorInfos(pass.colors.size());
            for (size_t i = 0; i < pass.colors.size(); ++i)
            {
                const ColorAttachment& ca = pass.colors[i];
                const TextureEntry& te = textures[ca.texture.id];

                colorInfos[i] = VkRenderingAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                colorInfos[i].imageView = te.view;
                colorInfos[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorInfos[i].loadOp = ca.loadOp;
                colorInfos[i].storeOp = ca.storeOp;
                colorInfos[i].clearValue = ca.clearValue;
            }

            VkRenderingAttachmentInfo depthInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            const bool hasDepth = !pass.depths.empty();
            if (hasDepth)
            {
                const DepthAttachment& da = pass.depths[0];
                const TextureEntry& te = textures[da.texture.id];
                depthInfo.imageView = te.view;
                depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthInfo.loadOp = da.loadOp;
                depthInfo.storeOp = da.storeOp;
                depthInfo.clearValue = da.clearValue;
            }

            // 尺寸：优先用显式指定的，否则取第一个附件的
            VkExtent2D extent = pass.extent;
            if (!pass.hasExtent)
            {
                const TextureHandle ref = pass.colors.empty() ? pass.depths[0].texture : pass.colors[0].texture;
                extent = textures[ref.id].desc.extent;
            }

            VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            info.renderArea.offset = { 0, 0 };
            info.renderArea.extent = extent;
            info.layerCount = 1;
            info.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
            info.pColorAttachments = colorInfos.empty() ? nullptr : colorInfos.data();
            info.pDepthAttachment = hasDepth ? &depthInfo : nullptr;

            vkCmdBeginRendering(cmd, &info);

            // viewport / scissor 在所有管线里都设成动态状态，必须显式设置
            cmdSetFullViewportAndScissor(cmd, extent);
        }

        // ---- 3) 用户回调（真正的绘制）----
        if (pass.callback)
            pass.callback(cmd);

        // ---- 4) 结束动态渲染 ----
        if (raster)
            vkCmdEndRendering(cmd);

        // ---- 5) 记录本 Pass 造成的最终状态，供下一个 Pass 推导屏障 ----
        for (const Access& acc : accesses)
        {
            TextureEntry& te = textures[acc.texture.id];
            te.layout = acc.layout;
            te.stage = acc.stage;
            te.access = acc.access;
        }
    }

    for (std::unique_ptr<Pass>& p : passes)
        p.reset();
    passes.clear();
    cmd = VK_NULL_HANDLE;
}

void RenderGraph::validateWriteCoverage() const
{
    // -----------------------------------------------------------------------
    // 序关系分析：谁在写、谁在读、读的时候有没有人写过。
    //
    // Pass 是严格按声明顺序执行的（这就是"线性渲染图"的含义），所以只要
    // 按顺序扫一遍就能得出准确结论，不需要真正的 DAG 分析。
    //
    // 顺序遍历时维护一个"已经被写过"的集合：
    //   * 遇到写 -> 加入集合
    //   * 遇到读 -> 若不在集合里，说明这一帧到目前为止没人写过它
    // -----------------------------------------------------------------------
    std::vector<bool> written(textures.size(), false);
    std::vector<bool> pendingWrite(textures.size(), false);   // 本 Pass 内部的写

    // 谁写过（包括被跳过的 Pass"本来会写"）——用来让报错能指名道姓。
    // 只说"没人写过"读者还得自己去翻是谁该写；直接告诉他
    // "SSAO 本来是由 Pass X 写的，但它这一帧被跳过了"，才是可行动的报错。
    std::vector<std::string> lastWriter(textures.size());

    for (const std::unique_ptr<Pass>& passPtr : passes)
    {
        const Pass& pass = *passPtr;

        // 这里有一个必须谨慎处理的地方：**被跳过的 Pass 也要参与"谁本该写"的统计**，
        // 否则报错时无法指出被跳过的那个写入者。但它不能算作"已经写过"。
        std::fill(pendingWrite.begin(), pendingWrite.end(), false);
        for (const ColorAttachment& ca : pass.colors)
            if (ca.texture.valid() && ca.texture.id < textures.size())
                pendingWrite[ca.texture.id] = true;
        for (const DepthAttachment& da : pass.depths)
            if (da.texture.valid() && da.texture.id < textures.size())
                pendingWrite[da.texture.id] = true;

        for (size_t i = 0; i < pendingWrite.size(); ++i)
        {
            if (!pendingWrite[i]) continue;
            lastWriter[i] = pass.enabled ? pass.name : (pass.name + "（本帧被 setEnabled(false) 跳过）");
            if (pass.enabled) written[i] = true;
        }

        if (!pass.enabled) continue;   // 被跳过的 Pass 不产生读

        for (const Access& acc : pass.accesses)
        {
            if (!acc.texture.valid() || acc.texture.id >= textures.size()) continue;

            const uint32_t id = acc.texture.id;
            const TextureEntry& te = textures[id];

            // 只查真正的"读"：写访问本身不要求之前有人写过
            if (acc.access & kWriteBits) continue;

            // 常驻资源允许整帧只读（IBL 烘焙结果就是这种）
            if (te.desc.persistent) continue;

            // 本 Pass 自己会写它（读改写）-> 内容来自本 Pass 或更早，没问题
            if (pendingWrite[id]) continue;

            if (!written[id])
            {
                std::string detail;
                if (!lastWriter[id].empty())
                    detail = "\n  它本来是由 Pass \"" + lastWriter[id] + "\" 写的。";
                else
                    detail = "\n  没有任何 Pass 声明过要写它。";

                throw std::runtime_error(
                    "RenderGraph 语义错误: Pass \"" + pass.name + "\" 读取了瞬时纹理 \""
                    + te.desc.name + "\"，但这一帧到目前为止没有任何 Pass 写过它。" + detail + "\n"
                    "  瞬时资源每帧内容都会重置，读到的将是未定义内容 —— 注意验证层对此不会报错\n"
                    "  （从 UNDEFINED 布局转出来本身是合法的），只能靠这条语义检查拦住。\n"
                    "  两种修法：\n"
                    "    ① 让写入者照常执行，把开关下沉到着色器（SSAOPass 的做法）；\n"
                    "    ② 调用方按帧如实声明：不需要这张图时就不要 read 它（PresentPass 对 Bloom 的做法）。");
            }
        }
    }
}

void RenderGraph::emitBarrierIfNeeded(const Access& acc)
{
    if (!acc.texture.valid()) return;

    TextureEntry& e = textures[acc.texture.id];

    const bool isRead = (acc.access & kWriteBits) == 0;
    const bool sameState = (e.layout == acc.layout && e.stage == acc.stage && e.access == acc.access);
    // 连续两次「完全相同的只读访问」之间不需要屏障；
    // 其余情况（含写->写）一律发屏障 —— 偏保守，但保证正确。
    if (sameState && isRead)
        return;

    // 额外保护：如果本次访问不需要换布局，而且上一状态的 srcStage/srcAccess
    // 都是空的，那么这条屏障在 Vulkan 里是「没有任何依赖项」的非法/无意义屏障
    // （sync1 要求 src 或 dst 至少有一项非 NONE，否则验证层会抱怨）。
    // 实际路径上很难触发，但渲染图现在是后效链的骨干，宁可在这里静默跳过，
    // 也不要留一个只在特定 Pass 顺序下才炸的雷。
    if (e.layout == acc.layout &&
        e.stage == VK_PIPELINE_STAGE_2_NONE && e.access == VK_ACCESS_2_NONE &&
        acc.stage == VK_PIPELINE_STAGE_2_NONE && acc.access == VK_ACCESS_2_NONE)
        return;

    VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    b.srcStageMask = e.stage;
    b.srcAccessMask = e.access;
    b.dstStageMask = acc.stage;
    b.dstAccessMask = acc.access;
    b.oldLayout = e.layout;
    b.newLayout = acc.layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = e.image;
    b.subresourceRange = { aspectOf(e.isDepth), 0, e.desc.mipLevels, 0, e.desc.arrayLayers };

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

RenderGraph::Pass& RenderGraph::addGraphicsPass(const std::string& name)
{
    passes.push_back(std::make_unique<Pass>(name));
    passes.back()->isCompute = false;
    return *passes.back();
}

RenderGraph::Pass& RenderGraph::addComputePass(const std::string& name)
{
    passes.push_back(std::make_unique<Pass>(name));
    passes.back()->isCompute = true;
    return *passes.back();
}

RenderGraph::TextureHandle RenderGraph::createTexture(const TextureDesc& desc)
{
    TextureEntry e;
    e.desc = desc;
    e.owned = true;
    e.isDepth = isDepthFormat(desc.format);
    // 常驻资源（在 desc 里显式标记）每帧不复位布局记录，否则会被当成
    // "内容未定义"而丢弃 —— 比如 IBL 的烘焙结果。
    e.resetEachFrame = !desc.persistent;
    createOwnedTexture(e);

    textures.push_back(std::move(e));
    return TextureHandle{ static_cast<uint32_t>(textures.size() - 1) };
}

RenderGraph::TextureHandle RenderGraph::importTexture(const TextureDesc& desc, VkImage image, VkImageView view,
                                                      VkImageLayout initialLayout)
{
    TextureEntry e;
    e.desc = desc;
    e.image = image;
    e.view = view;
    e.owned = false;
    e.isDepth = isDepthFormat(desc.format);
    e.layout = initialLayout;
    // 给了确定布局 = 跨帧常驻资源（例如 IBL cubemap），不要每帧复位。
    //
    // 注意这里把结论回写进了 desc.persistent，让"是否常驻"只有一个事实来源：
    // 否则 resetEachFrame（来自参数）和 desc.persistent（来自调用方）会各说各话，
    // 语义校验就会把 Environment/IBL 这类导入的常驻资源误判成"没人写的瞬时资源"
    //（这个误报在实现语义校验时真的发生过一次）。
    e.desc.persistent = (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED);
    e.resetEachFrame = !e.desc.persistent;

    textures.push_back(std::move(e));
    return TextureHandle{ static_cast<uint32_t>(textures.size() - 1) };
}

void RenderGraph::createOwnedTexture(TextureEntry& e)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = e.desc.format;
    info.extent = { e.desc.extent.width, e.desc.extent.height, 1 };
    info.mipLevels = e.desc.mipLevels;
    info.arrayLayers = e.desc.arrayLayers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = e.desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.flags = e.desc.flags;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(device.getVmaAllocator(), &info, &alloc, &e.image, &e.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("RenderGraph: 图像创建失败: " + e.desc.name);

    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = e.image;
    view.viewType = (e.desc.arrayLayers == 6 && (e.desc.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))
        ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = e.desc.format;
    view.subresourceRange = { aspectOf(e.isDepth), 0, e.desc.mipLevels, 0, e.desc.arrayLayers };

    if (vkCreateImageView(device.getDevice(), &view, nullptr, &e.view) != VK_SUCCESS)
        throw std::runtime_error("RenderGraph: 图像视图创建失败: " + e.desc.name);
}

VkImage RenderGraph::getImage(TextureHandle h) const
{
    if (!h.valid() || h.id >= textures.size()) return VK_NULL_HANDLE;
    return textures[h.id].image;
}

VkImageView RenderGraph::getView(TextureHandle h) const
{
    if (!h.valid() || h.id >= textures.size()) return VK_NULL_HANDLE;
    return textures[h.id].view;
}

VkFormat RenderGraph::getFormat(TextureHandle h) const
{
    if (!h.valid() || h.id >= textures.size()) return VK_FORMAT_UNDEFINED;
    return textures[h.id].desc.format;
}

VkExtent2D RenderGraph::getExtent(TextureHandle h) const
{
    if (!h.valid() || h.id >= textures.size()) return VkExtent2D{ 0, 0 };
    return textures[h.id].desc.extent;
}

const RenderGraph::TextureDesc& RenderGraph::getDesc(TextureHandle h) const
{
    if (!h.valid() || h.id >= textures.size())
        throw std::runtime_error("RenderGraph: 非法的纹理句柄");
    return textures[h.id].desc;
}

void RenderGraph::presentTransition(TextureHandle handle)
{
    if (!handle.valid()) return;
    Pass& p = addGraphicsPass("PresentTransition");
    // 没有颜色/深度附件 -> 渲染图只发屏障，不会 vkCmdBeginRendering
    p.transitionTo(handle, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void RenderGraph::setPassEnabled(const std::string& passName, bool enabled)
{
    for (std::unique_ptr<Pass>& p : passes)
        if (p->name == passName) p->enabled = enabled;
}

// ============================================================================
// Pass 的资源访问声明
// ============================================================================

RenderGraph::Pass& RenderGraph::Pass::transitionTo(TextureHandle t, VkImageLayout layout,
                                                  VkPipelineStageFlags2 stage, VkAccessFlags2 access)
{
    if (t.valid())
        accesses.push_back(Access{ t, layout, stage, access });
    return *this;
}

RenderGraph::Pass& RenderGraph::Pass::read(TextureHandle t, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
{
    if (t.valid())
        accesses.push_back(Access{ t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stage, access });
    return *this;
}

RenderGraph::Pass& RenderGraph::Pass::readStorage(TextureHandle t, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
{
    if (t.valid())
        accesses.push_back(Access{ t, VK_IMAGE_LAYOUT_GENERAL, stage, access });
    return *this;
}

RenderGraph::Pass& RenderGraph::Pass::writeStorage(TextureHandle t, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
{
    if (t.valid())
        accesses.push_back(Access{ t, VK_IMAGE_LAYOUT_GENERAL, stage, access });
    return *this;
}

RenderGraph::Pass& RenderGraph::Pass::writeColor(TextureHandle t, VkAttachmentLoadOp loadOp, VkClearValue clear,
                                                 VkAttachmentStoreOp storeOp)
{
    if (!t.valid()) return *this;

    ColorAttachment ca;
    ca.texture = t;
    ca.loadOp = loadOp;
    ca.storeOp = storeOp;
    ca.clearValue = clear;
    colors.push_back(ca);

    accesses.push_back(Access{ t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
    return *this;
}

RenderGraph::Pass& RenderGraph::Pass::writeDepth(TextureHandle t, VkAttachmentLoadOp loadOp, VkClearValue clear,
                                                 VkAttachmentStoreOp storeOp)
{
    if (!t.valid()) return *this;

    if (clear.depthStencil.depth == 0.0f && clear.depthStencil.stencil == 0)
        clear.depthStencil = { 1.0f, 0 };   // 深度默认清 1.0

    DepthAttachment da;
    da.texture = t;
    da.loadOp = loadOp;
    da.storeOp = storeOp;
    da.clearValue = clear;
    depths.push_back(da);

    accesses.push_back(Access{ t, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                               VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT });
    return *this;
}