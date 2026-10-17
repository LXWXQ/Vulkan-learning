# Sentinel 技术债清单

> 本文档只服务一件事：列出**当前已实现系统里尚未解决、且不依赖新主功能就能修**的技术债，
> 并给出优先级、证据、修法和验收标准。
>
> 新对话请先读 `HANDOFF.md` 和本文件；主功能与未来扩展看 `FUTURE_PLAN.md`。
> `AI_Readme.md` / `ARCHITECTURE.md` 是深入手册与设计文档，其中第 11/12 节的旧技术债清单
> 保留作历史证据，但**任务排序以本文件为准**。

## 0. 当前基线

- **工作区没有 `.git` 元数据**，不能依赖 `git log` / `git status`；以文件系统实际内容为准。
- 本机 Vulkan SDK：`D:/work/vulkan`，CMake 通过 `find_package(Vulkan REQUIRED)` 自动定位（不再有硬编码路径）。
- 已实现：D-1 ~ D-7（渲染图 / HDR+Bloom / bindless / 多帧在飞 / 异步 compute / Job System / EnTT ECS）
  + **调试 / 剖析基础设施 P0–P3**（验证层结构化、`spirv-val`、`--gpu-timing`、Tracy、RenderDoc、无头截图、
  深度校验作业、`VK_EXT_device_fault`、基线 diff、一键证据包）。
- 最近一次验证（2026-10-17）：
  - `tools/Validate.ps1`：**18 项引擎用例 + 2 项工具自测全 PASS**，stderr 全 0 字节。
  - `tools/Validate.ps1 -EnableProfiling`（Tracy / RenderDoc ON）：**19 项引擎用例 + 2 项工具自测全 PASS**。
  - `tools/DeepValidation.ps1`：`sync 0 errors / 0 warnings`、`bp 11`（良性）、`gpuav 2`、`printf 1` → PASS。
  - 正常窗口：约 500 FPS，`draw calls 104`，`desc binds 1`。

### 基线命令

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1 -EnableProfiling
powershell -NoProfile -ExecutionPolicy Bypass -File tools/DeepValidation.ps1 -Frames 60
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Baseline.ps1 -Action Diff
powershell -NoProfile -ExecutionPolicy Bypass -File tools/DebugBundle.ps1 -Frames 150
```

> 注意：`SOURCES` 使用 `CONFIGURE_DEPENDS`，但全新验证脚本仍会主动 configure；新增 `src/*.cpp` 后建议直接重跑 `tools/Validate.ps1`。

## 1. 优先级总表

| ID | 优先级 | 问题 | 影响 | 建议时机 |
|---|---|---|---|---|
| TD-01 | P0 | 依赖与验证脚本不可复现 | 新机器 clone 后无法可靠重建/回归 | 立即 |
| TD-02 | P0 | compute/graphics 跨队列 producer-consumer 同步缺失 | GPU culling / 体素 / indirect draw 无法真正落地 | 立即 |
| TD-03 | P1 | 窗口缩放 / swapchain 重建缺失 | out-of-date 直接抛异常，动态分辨率被堵死 | 动态分辨率/编辑器之前 |
| TD-04 | P1 | 材质槽位归还使用 vkDeviceWaitIdle | 热路径材质流送会全设备停顿 | 动态材质/流送之前 |
| TD-05 | P1 | RenderGraph 语义校验只到整张图/整个 buffer | mip/layer/子资源可能漏检 | 分层资源之前 |
| TD-06 | P2 | Bloom 关闭仍有 7 个全屏 pass | 关闭 Bloom 不省 GPU | Bloom 变重时 |
| TD-07 | P2 | ComputeProbePass 是固定开销探针 | 每帧固定 GPU 成本 | 真正 compute 任务接入时 |
| TD-08 | P2 | 遗留代码/废弃 shader/死接口 | 阅读成本、误用风险 | 清理窗口 |
| TD-09 | P2 | EngineConfig.version 没人读，无迁移/预设 | 配置格式变化时静默不兼容 | 配置扩展前 |
| TD-10 | P2 | RenderGraph 同步语义缺口（LOAD 读访问 / acquire 等待阶段）| syncval 报 hazard，会掩盖后续真实问题 | **已完成** |

### 不属于本清单的内容

以下内容属于未来主功能或随主功能一起做的能力，统一放在 `FUTURE_PLAN.md`：

- 地形、体素、GPU culling、indirect draw、实例化、LOD。
- glTF 内部 node 层级、动态子网格、资产流送。
- JobSystem 的 work stealing / 任务依赖图 / 取消。
- SpatialIndex 的 dirty / 增量更新 / BVH / 分块索引。
- 阴影、SSR、自动曝光、compute Bloom、动态分辨率。
- 物理、动画、音频、编辑器、Tracy、spdlog。
---

## 2. TD-01（P0）依赖与验证脚本不可复现

> **状态：已完成（2026-10-17）**。新增 `tools/SetupDependencies.ps1`、
> `tools/Validate.ps1` 与 `tools/README.md`；固定了 8 个第三方依赖版本，
> CMake 在 configure 阶段检查全部关键头文件，并把源码 glob 改为
> `CONFIGURE_DEPENDS`。一条命令即可完成依赖、全新构建与全部自检：
>
> ```powershell
> powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1
> ```
>
> 验证脚本检查每个测试的退出码与 stderr 字节数，并核对关键 stdout 标记。

### 现状

- `3rdParty/` 被 `.gitignore` 忽略；必须依赖本机已有目录。
- `tinygltf` 和 `EnTT` 是本次开发中手工下载的：
  - tinygltf 当前目录提交约为 `b3e0d78`；
  - EnTT 使用 v3.16.0（C++17 兼容）。
- `%TEMP%\sentinel\` 下的截图/回归脚本不存在。
- 目前只能人工运行命令并检查 stderr 字节数。

### 为什么急

换机器或新开对话时，最容易出现代码在但依赖不在、验证命令漏了一步、截图抓错窗口
这类假阴性。后续所有技术债修复都需要可靠证明。

### 建议修法

1. 新增 `tools/SetupDependencies.ps1`：
   - 检查并下载/解压固定版本的 tinygltf、EnTT；
   - 校验关键头文件存在；
   - 明确 spdlog 是可选/未启用，不要静默缺失。
2. 新增 `tools/Validate.ps1`：
   - 全新 configure + build；
   - 运行 `--jobtest`；
   - 运行四种 `--selftest 120`；
   - 检查 stderr 必须为 0；
   - 可选：启动正常窗口、发送 `WM_CLOSE`、确认退出码和配置保存。
3. 可选新增 `tools/README.md`，说明 Vulkan SDK 路径和 `Vulkan_ROOT` 覆盖方式。

### 验收标准

- 干净工作区执行一条命令即可完成依赖准备 + 构建 + 全部自检。
- 任意一步失败返回非零退出码。
- 不再依赖 `%TEMP%` 下的隐式脚本。
---

## 3. TD-02（P0）compute/graphics 跨队列 producer-consumer 同步

> **状态：已完成（2026-10-17）**。跨队列资源通过显式的
> `crossQueue=true` 走 `VK_SHARING_MODE_CONCURRENT` 简单路径；
> `RenderGraph` 按 pass 声明顺序增量推导 C->G / G->C 依赖，检测到双向依赖
> 会明确报错；`Renderer` 根据方向决定 submit 顺序，并用 timeline semaphore
> 插入跨队列 wait。新增 `CrossQueueProbePass` 交替验证两个方向：
> compute SSBO -> graphics，以及 graphics storage image -> compute。
> `--selftest 20 --cross-queue-test` 实测 stderr 0 字节，两个方向均输出
> `[TD02] ... verified`。

### 现状

`RenderGraph` 当前能检测跨队列资源访问，但会直接抛异常：

- compute 写 buffer，graphics 读（例如 indirect draw buffer）；
- graphics 写 buffer/image，compute 读（例如后处理 compute 读取 HDR）。

相关代码：

- `src/RHI/RenderGraph.cpp`：`validateWriteCoverage()`
- `src/RHI/Renderer.cpp`：`endFrame()` 双队列 submit
- `src/RHI/Device.cpp`：compute queue 选择

### 为什么急

这是 D-5 的未完成部分，也是 GPU culling / 体素生成 / Bloom compute 版的前置条件。
不修它，compute 只能做与 graphics 无关的探针任务，无法进入生产渲染路径。

### 建议修法

1. 资源跨队列二选一：
   - 简单路径：跨队列资源使用 `VK_SHARING_MODE_CONCURRENT`；
   - 规范路径：queue family ownership transfer + release/acquire barrier。
2. `Renderer::endFrame()` 根据 RenderGraph 标记得出：
   - graphics submit 是否需要 wait compute timeline；
   - compute submit 是否需要 wait graphics timeline。
3. 禁止同一帧出现双向依赖环；检测到环时明确报错。
4. 给 buffer 和 image 分别补跨队列验收测试。

### 验收标准

- compute 生成 SSBO/indirect buffer，graphics 下一 pass 消费，验证层 0 字节。
- graphics 写 storage image，compute 读取并写回，验证层 0 字节。
- 无交叉依赖时两条队列仍可并行。
---

## 4. TD-03（P1）窗口缩放与 swapchain 重建

> **状态：已完成（2026-10-17）**。窗口改为可缩放；acquire/present 对
> `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` 设置重建请求，应用在帧边界
> `vkDeviceWaitIdle` 后原地重建 swapchain、render-finished semaphore 与 ImGui 后端。
> `RenderGraph` 新增 owned texture 原位 resize / imported texture 视图更新；
> G-Buffer、SSAO、HDR、Bloom 与 Present 描述符按同一组 pass 顺序重建。
> 管线继续使用动态 viewport/scissor，不随分辨率重建；新增 `renderScale`
> 动态分辨率接口（G-Buffer/SSAO/HDR/Bloom 缩放，交换链不变）。
> `--selftest 60 --resize-test` 覆盖 4 次窗口缩放、0.5x/1.0x 动态分辨率切换和
> 最小化/恢复，验证层 stderr 0 字节。

### 现状

- `glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE)`。
- `Renderer::beginFrame()` 收到 `VK_ERROR_OUT_OF_DATE_KHR` 直接抛异常。
- 中间目标、管线、描述符 set 都在初始化时按固定尺寸写死。
- 需要重建：swapchain、G-Buffer、SSAO、HDR、Bloom、ImGui 描述符、相关 pipeline。

### 建议修法

1. 先把窗口改成可缩放，处理 `VK_SUBOPTIMAL_KHR` / out-of-date。
2. RenderGraph 增加 `destroyOwnedResources()` 或 `recreate(extent)`。
3. 各 Pass 提供 `resize(extent)`，重写依赖 extent 的描述符视图。
4. 交换链重建时等待所有 frame-in-flight 完成。

### 验收标准

- 拖动窗口尺寸后画面稳定，验证层 0 字节。
- 连续缩放/最小化/恢复不崩溃。
- 动态分辨率接口可以在不重建管线的情况下调整中间目标。

---

## 5. TD-04（P1）材质槽位归还的 vkDeviceWaitIdle

> **状态：已完成（2026-10-17）**。`releaseMaterialSet()` 不再调用
> `vkDeviceWaitIdle`，而是把槽位放入 deferred release 队列，记录归还时的最后
> 已提交 graphics frame。`DescriptorManager` 通过 Renderer 的 graphics timeline
> 查询已完成帧；只有 `safeAfterFrame <= completedFrame` 才把槽位放回 freeSlots。
> 材质 bindless 数组同时启用 `descriptorBindingSampledImageUpdateAfterBind` +
> `UPDATE_AFTER_BIND_POOL`，允许向已绑定的描述符数组继续写入新槽位。
> 空闲槽位耗尽时也只等待最老的、可能仍引用该槽位的 graphics 提交，而不是停整台设备。
> 新增 `--material-stress`：每帧分配一个槽位、延迟归还上一帧槽位，150 轮后
> `used_before=26 used_after=26 max_pending=2 capacity=256`，验证层 stderr 0 字节。

### 现状

`DescriptorManager::releaseMaterialSet()` 归还 bindless 数组槽位前执行 `vkDeviceWaitIdle`。
这保证正确性，但会让整个设备停顿。

### 建议修法

- 加材质更新 fence / timeline 延迟回收队列；或
- 使用 `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` + 双缓冲描述符数组。

### 验收标准

- 运行期加载/卸载材质时没有 GPU use-after-free，也不触发 device-wide wait。
- 场景反复加载 100+ 次不耗尽槽位。

---

## 6. TD-05（P1）RenderGraph 子资源语义校验

> **状态：已完成（2026-10-17）**。`RenderGraph` 的纹理访问新增
> `SubresourceRange`，每张纹理按 mip/layer 维护独立 layout/stage/access 状态；
> barrier 按实际声明的子资源逐项发 `VkImageMemoryBarrier2`，不会误转整个 mip 链。
> 写覆盖校验也按子资源进行：写 mip0/layer0 后再读 mip1/layer0 会明确报错并指出
> mip/layer；buffer access 同样支持 `BufferRange` 的 offset/size 与区间覆盖检查。
> 新增 `--subresource-test` 正向探针（mip0-layer0 / mip1-layer1 + 两段 buffer）
> 和 `--subresource-error-test` 反例；两者验证层 stderr 均为 0 字节。

### 现状

- `validateWriteCoverage()` 以整张 image / 整个 buffer 为单位。
- 单个 mip / array layer 的读写没有独立跟踪。
- `VkImageMemoryBarrier2` 当前总是覆盖 `baseMipLevel=0, levelCount=mipLevels, layerCount=arrayLayers`。

### 建议修法

- 引入 `SubresourceRange`，把纹理访问按 mip/layer 分桶；
- 为每个 access 记录 range，并在 barrier 阶段合并相邻 range；
- buffer 支持 offset/size 区间。

### 验收标准

- 单独写 mip0、读 mip1 的资源会报出明确语义错误。
- 多 mip 资源只转换实际使用的 mip，不误伤其它 mip。
---

## 7. TD-06（P2）关闭 Bloom 的固定 Pass 开销

> **状态：已完成（2026-10-17）**。Bloom 关闭时 `BloomPass` 直接返回，不再注册
> Bright / 3×Down / 3×Up，共省掉 7 个全屏 Pass。PresentPass 现在有两套路径：
> Bloom 开时读 HDR+Bloom 并走原 `present.frag`；关闭时只读 HDR，使用独立的
> 单绑定描述符布局和 `present_no_bloom.frag` 管线，不再绑定或声明 bloomResult。
> 两种路径都通过动态 viewport/scissor 和现有 resize 流程。
> `selftest-no-bloom` / `selftest-no-bloom-no-ssao` 现在必须出现
> `[TD06] Bloom disabled: skipping Bright + Down + Up passes`，且 stderr 0 字节。
> 同机 Debug、3000 帧实测：Bloom 开 455 FPS，新关闭路径 531 FPS（约 +17%），
> 两轮 stderr 均为 0。

### 现状

W2 为了让语义校验成立，Bloom 关闭时仍执行：
1 个亮部提取 + 3 个降采样 + 3 个上采样，只是强度置零。

### 建议修法

PresentPass 准备两套描述符绑定：

- Bloom 开：读取 bloomResult；
- Bloom 关：只读 hdrScene，不声明 bloomResult。

这样 Bloom 可以真正跳过所有相关 Pass。

### 验收标准

- Bloom 开启画面与当前一致；
- Bloom 关闭时 RenderGraph 不注册 Bloom pass，验证层仍为 0 字节；
- GPU 耗时相比当前关闭路径下降。

---

## 8. TD-07（P2）ComputeProbePass 的固定开销

> **状态：已完成（2026-10-17）**。`ComputeProbePass` 默认关闭，不再注册两个
> compute dispatch 和 CPU 读回；输出一次
> `[TD07] ComputeProbe disabled: no probe dispatches or CPU readback`。
> 新增 `--compute-test` 显式启用独立回归，仍会输出
> `[D5] async compute probe verified: ...`。同机 Debug、3000 帧实测：
> 默认 486 FPS，开启探针 455 FPS（约 +6.5%）；两轮 stderr 均为 0。

### 现状

`ComputeProbePass` 是 D-5 的验收探针，每帧固定跑两个 compute dispatch + CPU 读回。
它不是生产任务，但当前默认执行。

### 建议修法

- 换成真正的 compute 工作（culling / 体素 / Bloom）；
- 或者用 `EngineSettings` / 命令行开关控制，默认关闭；
- 保留 `--compute-test` 形式的独立回归入口。

### 验收标准

- 默认运行不再为探针付出固定成本；
- 独立 compute 回归仍能证明 GPU 写 + CPU 读回路径有效。

---

## 9. TD-08（P2）遗留代码与废弃接口

> **状态：已完成（2026-10-17）**。删除 `src/Core/Descriptor.h/.cpp`；删除
> `light_cube.*`、`shader.*` 和 shader 源目录全部旧 `.spv`；移除 `Swapchain` 的
> depth/Framebuffer 死接口与成员；清理 `IBLBaker` 的 RenderPass/Framebuffer 残留
> 和空的 `createRenderPasses()`；删除 `Renderer` 的 `#if 0` 旧 RenderPass 接口。
> `tools/Validate.ps1` 新增 TD-08 dead-file guard，防止这些文件/源目录 `.spv`
> 重新混进仓库。干净 build + 全部回归通过。

### 现状

- `src/Core/Descriptor.h/.cpp` 不在渲染路径。
- `shaders/light_cube.*`、`shaders/shader.*` 无人引用。
- `Swapchain` 里 depth / framebuffer 死接口仍然存在。
- `IBLBaker` 保留 RenderPass / Framebuffer 残留代码。
- shader 源目录仍有旧的 `.spv` 构建产物。

### 建议修法

- 分两步：先确认无引用，再删除；删除后用 clean build + 四种 selftest 验证。
- `AI_Readme.md` 对应条目同步删除。

### 验收标准

- 无引用死代码清零；
- 构建输出不引用旧 shader；
- 行为不变，验证层 0 字节。

---

## 10. TD-09（P2）EngineConfig 版本迁移

> **状态：已完成（2026-10-17）**。配置当前版本提升到 2，加载时显式执行
> v0 -> v1（把旧顶层字段收进 `engineSettings`）-> v2（补齐 renderScale/planned）。
> 迁移前把原文件复制为 `.vN.bak`，迁移成功后原子写回。未来版本、损坏 JSON、
> 非法 version 都会保留原文件；`save()` 对不可读的已有配置直接拒绝覆盖。
> 新增 `--config-test`，覆盖 v0->v2 迁移、备份、损坏文件与 version 999 不覆盖。

### 现状

`config/engine.json` 有一个 `version: 1` 字段，但加载代码完全不检查它。
字段重命名/删除时，旧配置可能被静默当成默认值，或者更糟，某些语义变了但没有提示。

### 建议修法

- 读取 `version`；
- 支持 `version < current` 的显式迁移函数；
- 无法迁移时保留备份并提示用户恢复默认值。

### 验收标准

- 旧版本配置能明确提示迁移结果；
- 损坏配置不会覆盖原文件。
---

## 11. TD-10（P2）RenderGraph 同步语义缺口（已完成）

### 现状（修复前）

`--validation-features sync`（同步校验）在默认场景报出两类 hazard：

- `SYNC-HAZARD-READ-AFTER-WRITE` ×10：`writeColor` / `writeDepth` 无论 `loadOp` 是什么都只声明 `*_WRITE` 访问位，而 `VK_ATTACHMENT_LOAD_OP_LOAD` 会**读取**附件原内容（Bloom 的 Up 级累加正是 LOAD）。
- `SYNC-HAZARD-WRITE-AFTER-READ` ×5：graphics 等待 `imageAvailable` 的 stage 只到 `COLOR_ATTACHMENT_OUTPUT`，而交换链图像的布局转换可能发生在更早的 stage，与 `vkAcquireNextImageKHR` 竞争。

### 为什么必须修

这两类在多数驱动上"良性"（执行依赖其实存在），但：

1. 它们是真实的同步语义缺口，属于规范层面的欠声明；
2. 更关键的是——**新引入的 hazard 会淹没在旧噪声里**，等于失去同步校验这个工具。任何 GPU-driven / 跨队列特性开工前，这条基线必须是干净的。

### 修法

1. `Pass::writeColor` / `Pass::writeDepth`：`loadOp == LOAD` 时把 `*_ATTACHMENT_READ_BIT` 一并 OR 进访问位；
2. `Renderer` 中 `imageAvailable` 的 `pWaitDstStageMask` 从 `COLOR_ATTACHMENT_OUTPUT` 放宽到 `ALL_COMMANDS`。

### 验收标准（已达成）

- `--validation-features sync` 报 **0 errors / 0 warnings**（5 帧与 30 帧均验证）；
- `tools/Validate.ps1` 全绿、stderr 0 字节；
- `tools/DeepValidation.ps1` 中 `sync` 模式作为门禁项通过。

### 顺带记录

`--validation-features bp` 余 11 条 warning 均为良性提示（VMA 专用块绑定 ×11）；SPIR-V `WorkgroupSize` 弃用已通过 `--target-env vulkan1.3` 清除。

---

## 12. 建议解决顺序

1. ~~**TD-01**：先把构建与验证做成可重复的一条命令。~~ 已完成。
2. ~~**TD-02**：补 compute/graphics 跨队列同步，这是后续 GPU 驱动渲染的前置条件。~~ 已完成。
3. ~~**TD-03**：补 swapchain 重建，解锁动态分辨率和窗口缩放。~~ 已完成。
4. ~~**TD-04**：处理材质生命周期。~~ ~~**TD-05**：子资源语义校验。~~ 均已完成；
   下一项从 **TD-06** 开始。
5. ~~**TD-06**：关闭 Bloom 的固定 pass 开销。~~ ~~**TD-07**：ComputeProbePass
   固定开销。~~ 均已完成。
6. ~~**TD-08**：遗留代码与废弃接口。~~ ~~**TD-09**：配置版本迁移。~~ 已完成。
7. ~~**TD-10**：RenderGraph 同步语义补全（LOAD 读访问位 + acquire 等待阶段）。~~ 已完成；
   当前技术债清单已清空（syncval 基线为 0 errors）。

每修完一项，至少跑：

```powershell
.\build\Debug\VulkanEngine.exe --jobtest
.\build\Debug\VulkanEngine.exe --selftest 120
.\build\Debug\VulkanEngine.exe --selftest 120 --no-bloom
.\build\Debug\VulkanEngine.exe --selftest 120 --no-ssao
.\build\Debug\VulkanEngine.exe --selftest 120 --no-bloom --no-ssao
```

并确认 stderr 全为 0；涉及画面/性能的改动还要记录同机位截图或 `[Perf]` 数据。