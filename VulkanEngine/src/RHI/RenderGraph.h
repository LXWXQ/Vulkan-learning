#pragma once

#include "Device.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

// ============================================================================
// 线性渲染图
//
// 目标：把「手写 vkCmdPipelineBarrier」从每个 Pass 里彻底移除。
//
//   graph.beginFrame(cmd);
//     auto& p = graph.addGraphicsPass("BloomDownsample");
//     p.read(bloomSrc);                              // 声明采样 -> 自动插 SHADER_READ 屏障
//     p.writeColor(bloomDst, LOAD_OP_DONT_CARE);     // 声明渲染目标 -> 自动插 COLOR_ATTACHMENT 屏障
//     p.execute([](VkCommandBuffer c) { /* 绘制 */ });
//   graph.endFrame();
//
// 每个资源会记住「上一次被谁、以什么布局访问」，下次访问时自动插入最小必要的屏障。
//
// 为什么叫「线性」：Pass 严格按声明顺序执行，不做重排 / 别名化 / 自动剔除。
// 这已经能解决 90% 的痛点（不再手写屏障），而复杂度和调试难度远低于完整 DAG 渲染图。
// 将来要加自动剔除 / 内存别名化时，这个结构也留了位置。
//
// 配合 Vulkan 1.3 的 dynamic rendering：不再有 VkRenderPass / VkFramebuffer，
// 每个 Pass 只是 vkCmdBeginRendering + 一组 imageView —— 也就不会再出现
// 「Framebuffer 生命周期」那一类坑。
// ============================================================================
class RenderGraph
{
public:
    struct TextureHandle
    {
        uint32_t id = UINT32_MAX;
        bool valid() const { return id != UINT32_MAX; }
    };

    struct TextureDesc
    {
        std::string name;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        VkImageUsageFlags usage = 0;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkImageCreateFlags flags = 0;

        // 每帧内容都会被重新生成，还是跨帧常驻？
        //
        // 这个标记是渲染图"语义校验"的依据（见 endFrame 里的检查）：
        //   瞬时资源（false）—— 每帧都必须至少有一个 Pass 写它。内容不会被保留，
        //                      所以"没人写"就等于"下游读到未定义内容"。
        //   常驻资源（true） —— 初始化时写一次，之后每帧只读（IBL 烘焙结果就是
        //                      这种），允许整帧没有写入者。
        //
        // 为什么不能靠"有没有被写"自动判断：那样只能事后发现，而这里要的是
        // 在错误发生的那一帧直接报出来。
        bool persistent = false;
    };

    // 一次资源访问：目标布局 + 在哪个阶段 + 什么访问类型
    struct Access
    {
        TextureHandle texture;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stage = 0;
        VkAccessFlags2 access = 0;
    };

    struct ColorAttachment
    {
        TextureHandle texture;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearValue{};
    };

    struct DepthAttachment
    {
        TextureHandle texture;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearValue{};
    };

    class Pass
    {
    public:
        Pass(std::string name) : name(std::move(name)) {}

        // ---- 资源访问声明（会被翻译成屏障）----

        // 作为纹理采样（片元着色器）
        Pass& read(TextureHandle t,
                   VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VkAccessFlags2 access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        // 作为颜色附件写入
        Pass& writeColor(TextureHandle t,
                         VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                         VkClearValue clear = {},
                         VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

        // 作为深度附件写入
        Pass& writeDepth(TextureHandle t,
                         VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                         VkClearValue clear = {},
                         VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

        // 作为存储图像（compute / 计算写入）
        Pass& writeStorage(TextureHandle t,
                           VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VkAccessFlags2 access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        /// 计算着色器读取存储图像
        Pass& readStorage(TextureHandle t,
                          VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VkAccessFlags2 access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        // 只做布局转换，不作为渲染目标。
        // 典型用途：把交换链图像转到 PRESENT_SRC_KHR 交给 present。
        Pass& transitionTo(TextureHandle t, VkImageLayout layout,
                           VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE,
                           VkAccessFlags2 access = VK_ACCESS_2_NONE);

        Pass& execute(std::function<void(VkCommandBuffer)> fn) { callback = std::move(fn); return *this; }

        Pass& setEnabled(bool e) { enabled = e; return *this; }
        bool isEnabled() const { return enabled; }
        const std::string& getName() const { return name; }

        // 不指定时，渲染图会用第一个颜色附件的尺寸自动设置 viewport / scissor
        Pass& setExtent(VkExtent2D e) { extent = e; hasExtent = true; return *this; }

    private:
        friend class RenderGraph;
        std::string name;
        std::vector<Access> accesses;
        std::vector<ColorAttachment> colors;
        std::vector<DepthAttachment> depths;
        std::function<void(VkCommandBuffer)> callback;
        bool enabled = true;
        bool isCompute = false;
        bool hasExtent = false;
        VkExtent2D extent{};
    };

    explicit RenderGraph(Device& device);
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // ---- 帧生命周期 ----
    void beginFrame(VkCommandBuffer commandBuffer);
    void endFrame();     // 按顺序执行所有 Pass

    // ---- Pass 注册 ----
    Pass& addGraphicsPass(const std::string& name);
    Pass& addComputePass(const std::string& name);

    // ---- 资源 ----
    // 由渲染图创建并持有（用于 Bloom 之类需要中间目标的场景）
    TextureHandle createTexture(const TextureDesc& desc);

    // 导入外部已有的资源（交换链图像、G-Buffer 等），生命周期由调用方管理
    TextureHandle importTexture(const TextureDesc& desc, VkImage image, VkImageView view,
                                VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED);

    VkImage     getImage(TextureHandle h) const;
    VkImageView getView(TextureHandle h) const;
    VkFormat    getFormat(TextureHandle h) const;
    VkExtent2D  getExtent(TextureHandle h) const;
    const TextureDesc& getDesc(TextureHandle h) const;

    // 供 ImGui 面板开关某个 Pass
    void setPassEnabled(const std::string& passName, bool enabled);

    // 把某个纹理转到 PRESENT_SRC_KHR，必须在本帧最后一个 Pass 之后调用。
    // 渲染图不应该知道「哪张图要被 present」，所以由应用显式声明。
    void presentTransition(TextureHandle handle);

private:
    struct TextureEntry
    {
        TextureDesc desc;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        bool owned = false;
        bool isDepth = false;
        // true = 每帧开始把布局记录复位成 UNDEFINED（瞬时资源）；
        // false = 跨帧常驻的只读资源（如 IBL cubemap），复位会被当成
        //         「内容未定义」而丢弃。
        bool resetEachFrame = true;

        // 上一次访问后的状态，用来推导下一次的屏障
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
    };

    void createOwnedTexture(TextureEntry& entry);
    void emitBarrierIfNeeded(const Access& acc);

    // 语义校验：瞬时资源每帧必须有人写。
    // 与屏障推导（同步正确性）是两件不同的事 —— 这个查的是"语义正确性"，
    // 验证层对此一言不发（从 UNDEFINED 布局转出来本身完全合法）。
    void validateWriteCoverage() const;

    Device& device;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::vector<TextureEntry> textures;
    std::vector<std::unique_ptr<Pass>> passes;
};