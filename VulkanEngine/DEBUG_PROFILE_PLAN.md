# Sentinel Vulkan Engine - Debug / Profile 计划

> 更新时间：2026-10-17
> 状态：**P0/P1 已实施并验证；P2 仅保留厂商中立项（深度校验作业 + device fault）**
> 定位：为「由大模型负责编码与调试」的工作流，预先设计好调试、性能检测、性能优化的工具链、阶段划分、Artifact 规格与验收标准。
> 阅读顺序：先读 §0 与 §1，再按需进入 §3（工具评估）、§4（排障矩阵）、§5（阶段计划）、§6（Artifact 规格）、§7（LLM 调试工作流）。
> **进度（2026-10-17）**：**P0 / P1 已实施并验证**；**P2 只保留厂商中立项并已实施**——P2-3（深度校验作业：sync/bp/gpuav/printf）、P2-4（`VK_EXT_device_fault`）；P2-1/P2-2（nsys/ncu）降级为 §3 G 组可选补充。**P3-1（一键证据包）/ P3-2（基线 + diff）已实施**。默认构建 18 项引擎用例 + 2 项工具自测全绿；`Validate.ps1 -EnableProfiling` 19 项引擎用例 + 2 项工具自测全绿，stderr 全 0 字节。

---

## 0. 30 秒结论

- 本项目已经有一套成熟的「无头验证」体系：`tools/Validate.ps1` + `--selftest` + stdout 标记 + **stderr 0 字节** 基线。本计划的每个工具都必须长在这套体系上，而不是另起炉灶。
- 选工具的第一原则是 **可被大模型消费**：headless 触发、机器可读输出、artifact 落盘、退出码语义、**绝不污染 stderr**。
- **开关原则**：调试 / 剖析能力「随用随开」，关闭时对运行时性能**零影响**（编译期 + 运行期两层开关，详见 §1.5）。
- 分四阶段推进：**P0 零依赖快速见效** → **P1 帧捕获与时间线** → **P2 深度性能剖析** → **P3 调试包与自动化封装**。
- 环境现状（已实测）：SPIRV-Tools 全家桶、`vulkaninfo`、`vkconfig` **已随 Vulkan SDK 装好但未接入**；RenderDoc / Tracy / Nsight **需单独安装**。
- 最大隐患：`src/RHI/Device.cpp` 的验证层回调直接写 `std::cerr`，与「stderr 0 字节」基线天然冲突，**必须在 P0 优先修掉**。

---

## 1. 背景、目标与约束

### 1.1 为什么需要这份文档

当前项目可观测性只有两类：

1. **CPU 侧**：帧循环里的 `update` / `render` 两段计时（每 5 秒一行 `[Perf]`）。
2. **场景统计**：`RenderTelemetry` 的 draw calls / descriptor binds / triangles / vertices。

缺的是：**GPU 侧每个 pass 的真实耗时**、**帧内容级的捕获与回放**、**CPU/GPU 队列重叠情况**、**着色器内部状态**。

因为编码与调试都由大模型完成，工具的价值不在于「人能看着 GUI 分析」，而在于「**结论能变成磁盘上的文本文件，被大模型读进来推理**」。所以本计划刻意优先选择 CLI 与机器可读输出。

### 1.2 需求分层

| 层 | 要回答的问题 |
|---|---|
| 正确性调试 | 为什么花屏 / 黑屏 / 崩溃？验证层报了什么？同步有没有竞争？子资源有没有写错？ |
| 性能检测 | 每帧 CPU / GPU 各花在哪？哪个 pass 最贵？队列有没有串行化？ |
| 性能优化 | 热点在哪一行 / 哪个 pass？改完是变快还是变慢？ |

### 1.3 硬约束（不可破坏）

1. **stderr 0 字节**：所有预期错误必须被测试在进程内捕获，不得让 `std::cerr` 冒泡。（现有 `debugCallback` 违反此约束，见 P0-1。）
2. **退出码语义 + stdout 标记**：成功/失败靠退出码与 stdout 标记，不靠人眼。
3. **默认零开销（随用随开）**：所有调试 / 剖析功能默认关闭；**关闭状态下对运行时性能零影响**——不做任何分配、不插任何命令、不加任何 hook、不建任何 query，行为与现状**字节级一致**。详见 §1.5。
4. **不新增入库的大体积二进制**：`3rdParty/` 不入库；工具走 SDK、独立安装或 `-D` 参数指向。
5. **不与既有约定冲突**：Pass 资源声明、RenderGraph 子资源校验、配置 version 2 等既有规矩保持不变。
6. **无 git**：不能依赖 `git log/status`；用**带时间戳的 artifact + 已知良好基线**来做 diff 与回溯。

### 1.4 非目标

- 不做 GUI 可视化面板，也不把 ImGui 面板当作调试结论的来源。
- 不把调试工具带进发布（Release）路径。
- 不做跨平台（Windows 桌面已确认）。
- 不在本阶段实现渲染新特性本身（阴影 / SSR 等），只做「调试与剖析的基础设施」。

---

### 1.5 零开销开关模型：随用随开，关时零影响

**总原则**：调试 / 剖析能力必须「需要时打开、不需要时彻底消失」，且**关闭状态下对运行时性能零影响**——不是「只是不输出」，而是**不分配、不插命令、不加 hook、不建 query、不启线程**。

> **决策（2026-10-17）**：不追求「绝对零开销」。采用判据 **「明显影响渲染速度的绝对不能留」**——允许每帧每 pass 一处 hoisted 判断与少量常驻元数据，但**禁止任何可测量的渲染性能回退**。

**两层正交开关**：

| 层 | 由谁控制 | 关闭时成本 | 适用对象 |
|---|---|---|---|
| **编译期开关** | CMake option / `SENTINEL_*` 宏 | **完全为 0**，代码根本不参与编译 | 重量级 / 带外部依赖：Tracy、RenderDoc hook、Debug Printf、GPU-AV |
| **运行期开关** | CLI flag（默认关）/ `EngineSettings` | 每帧每 pass 一个 hoisted 的 `if`（热点内层零判断） | 轻量自研计量：`--gpu-timing`、`--screenshot` |

**开关分层表（建议默认值）**：

| 能力 | 编译期默认 | 运行期默认 | 关闭时成本 | 备注 |
|---|---|---|---|---|
| 验证层 | Debug 开 / Release 关（`NDEBUG`） | — | Release 为 0 | **沿用现有模型**，本身就是零开销范本 |
| Best Practices / syncval / GPU-AV / Debug Printf | 可编译进 | 关 | 0（未启用即不创建） | 走 layer settings / VkValidationFeatures |
| `--gpu-timing` | 编译进 | 关 | 0 分配、0 命令 + 一处 hoisted 分支 | 追求绝对零分支可加 `SENTINEL_ZERO_OVERHEAD` |
| `--screenshot` | 编译进 | 关 | 0 | 仅 flag 下写 PNG |
| Tracy | **默认 OFF**（`SENTINEL_TRACY`） | 由 `TRACY_ENABLE` 决定 | 宏展开为 `(void)0`，**真 0** | 需单独 build 或打开 option |
| RenderDoc in-app hook | **默认 OFF**（`SENTINEL_RENDERDOC`） | `--capture-frame` | 0（连 `RENDERDOC_GetAPI` 都不调用） | 关闭时不加载 injectable |
| 外部注入（nsys / RenderDoc 外挂 / Nsight） | 不参与编译 | 由 wrapper 单次设置 | 0 | **绝不用 vkconfig 全局常挂 layer** |

**写代码时必须遵守的 9 条规则**：

1. **默认关**：开关一律正着命名（`--gpu-timing`），不用 `--no-...`；默认值 = 不开启。
2. **关闭时不分配**：`if (!enabled) return;` 必须在**创建 query pool / buffer / descriptor / 线程之前**；禁止「先建好、只是不读取」。
3. **关闭时不插命令**：timestamp、Tracy zone、debug label 一律不写入 command buffer。
4. **关闭时不改生命周期**：不得因「关着」而少建 / 多建任何同步对象或资源；VMA 统计与帧时序必须与现状一致。
5. **热路径零分支**：开关判断只放在**帧边界 / 每 pass 一次**的层级，禁止进入 per-draw / per-object 内层循环。
6. **C++17 约束**：不能用 C++20 的 `[[likely]]`；靠「判断提到循环外 + 分支预测」即可。
7. **Release 更严**：`NDEBUG` 下验证层、Debug Printf、GPU-AV 一律不编译 / 不启用。
8. **可自证**：提供 `--build-info`（或启动横幅），打印**本二进制编译进了哪些能力**与**本次运行开启了哪些**，杜绝「以为开了其实没编译进去」。
9. **外部工具只作用于单次**：nsys / RenderDoc 经 `tools/Profile.ps1` 在单次进程环境注入，不做全局常驻 layer。

**守卫测试（防止悄悄退化成「常开」）**：

- 断言：默认（无 flag）运行相对基线版本，**新增的资源 / query pool / 文件产物数量均为 0**。
- 断言：`--gpu-timing` 关闭时**不创建任何 timestamp query pool**（自计数 + VMA 统计双重校验）。
- 建议：Release 下跑一段 `--selftest`，确认 FPS 与「未接入调试基础设施前」同量级（同机相对比较）。

---

## 2. 评估框架：什么叫「LLM 友好」

对每个候选工具按 7 条打分（1-5），总分决定优先级：

| # | 维度 | 说明 |
|---|---|---|
| 1 | Headless 触发 | PowerShell 一条命令能跑，不需要人点 GUI |
| 2 | 机器可读输出 | JSON / CSV / SQLite > 文本报告 >>> 截图 |
| 3 | 可复现 | 能固定帧数 / 场景 / renderScale，同样输入同样输出 |
| 4 | Artifact 落盘 | 结果写成文件，可被 `Get-Content` 读取 |
| 5 | 退出码语义 | 成功失败可判定 |
| 6 | 不污染 stderr | 满足项目硬约束 |
| 7 | 可二次分析 | 有 Python API / CLI query / SQL |
| 8 | **厂商中立** | 不依赖特定 GPU 厂商（NVIDIA/AMD/Intel）；换卡后仍可用 |

**判定档位**：`采纳`（进计划）、`可选`（按需开启，不进默认回归）、`跳过`（不适用或有更好替代）。

---

## 3. 候选工具实用性评估

统一卡片包含：定位 / 触发方式 / 输出与消费 / 能力边界 / 集成成本 / 风险 / 结论。

### A 组 · 验证层家族（已有 `VK_LAYER_KHRONOS_validation`，只用了约 10%）

**共同点**：全部通过调试回调输出，所以 A 组的**可用性完全取决于 P0-1 的回调改造**。若回调仍是 `std::cerr`，A 组任何增强都会破坏 stderr 基线。

#### A1 标准校验（已用）
- 定位：API 误用、参数、生命周期、layout。
- 结论：**采纳（已用）**。P0。

#### A2 Best Practices
- 开关：`VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT`。
- 定位：性能反模式检测（例如每帧重建 pipeline、可 memoryless 的附件、次优 barrier 等）。
- 输出：验证层消息 → 转成 `validation.jsonl` 后可被大模型当「优化建议清单」读。
- 风险：噪声较多，需要按 severity 过滤；会拖慢启动与运行。
- 结论：**可选**。P0-2（做成 flag，默认关）。

#### A3 Synchronization Validation
- 开关：`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`。
- 定位：RAW / WAR / WAW 数据竞争、barrier 缺失。
- 对本项目的特殊价值：你们的 **RenderGraph 自动 barrier** 是核心卖点，syncval 恰好是它的天然反例检验器。
- 能力边界：对非常规访问模式可能误报；开销大；某些查询/统计用法可能不被覆盖。
- 风险：可能对「瞬态资源 + timeline 延迟回收」这类模式产生噪声，需要实机校准。
- 结论：**可选（强烈建议纳入「深度回归」而非默认回归）**。P0-2 / P2-3。

#### A4 GPU-Assisted Validation (GPU-AV)
- 开关：`VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT`（可能还需预留 binding slot 的配套 feature）。
- 定位：着色器运行期越界读 / 写 descriptor。
- 对本项目的特殊价值：你们是 **bindless 材质数组**（albedo/normal/metallic/roughness），最大风险就是槽位越界；GPU-AV 正是抓这个的。
- 能力边界 / 风险：与 `descriptorBindingSampledImageUpdateAfterBind`（bindless）的兼容性需实机验证；开销显著；可能改变 descriptor 布局假设。
- 结论：**可选（bindless 专项排查时手动开）**。P2-3。

#### A5 Debug Printf
- 开关：`VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT`；GLSL 侧需要 `GL_EXT_debug_printf`，SPIR-V 侧走非语义信息。
- 定位：**让着色器能「打日志」**。这是把「GPU 内部黑箱」变成「可读文本」的关键手段，对 LLM 极友好。
- 输出：验证层消息（因此同样依赖回调改造）。
- 风险：需要 `VK_KHR_shader_non_semantic_info` 等前置（核心 1.3 下情况以 SDK 版本为准）；默认不能常开。
- 结论：**采纳（做成 flag，默认关）**。P1-3。

#### A6 Shader Instrumentation（`VK_LAYER_KHRONOS_shader_instrumentation`）
- 定位：给 SPIR-V 打桩（配合 debug printf / GPU-AV 的底层机制）。
- 结论：**跳过**（作为 A4/A5 的底层细节，不单独接入）。

#### A7 结构化输出与 vkconfig
- 定位：把验证层消息写成结构化 JSON；用 `vkconfig` 生成 / 切换 layer settings JSON，做「开/关某能力」的 A/B。
- 结论：**采纳**。P0-1 / P0-2。

---

### B 组 · 帧捕获与逐帧分析

#### B1 RenderDoc（MIT 开源）—— 帧级调试主力
- 定位：逐帧捕获 + 回放，查看每个 draw 的 pipeline state、绑定、附件、像素历史。
- **触发方式（LLM 友好点）**：
  1. **CLI**：`renderdoccmd capture`（配合 `--wait-for-exit` / 子进程 hook），可无人值守抓帧。
  2. **In-app API**：`RENDERDOC_API_1.x`（`TriggerCapture` / `SetCaptureFilePathTemplate`）→ 建议给引擎加 `--capture-frame N`，精准抓「Bloom 关闭那帧」「resize 之后那帧」。
  3. **Python API**：`qrenderdoc --python analyze.py` 或 `renderdoc` 模块直接读 `.rdc`，脚本导出 drawcall 列表 / 资源绑定 / pipeline 元数据为 **JSON / Markdown**。
- 输出与消费：`.rdc` 是二进制（大模型读不懂），**价值在于 Python 导出的文本摘要**。
- 能力边界：GUI 视图对人有用、对 LLM 无用；对 bindless 的海量 descriptor，其资源视图展示能力有限。
- 集成成本：中。(a) 引擎侧加 in-app API + `--capture-frame`（约 100 行）；(b) `tools/Capture.ps1` 包装 `renderdoccmd`；(c) 一个 python 导出脚本。
- 风险：注入可能影响 present 时机，建议配合 `--selftest` 无头抓帧；与 bindless 的兼容性一般没问题但需实测。
- 结论：**采纳（帧捕获主力）**。P1-2。

#### B2 NVIDIA Nsight Graphics
- 定位：GPU Trace + 帧调试器，深度图形分析，Windows 免费（RTX 2070 适用）。
- 触发方式：以 **GUI 为主**，CLI 自动化能力弱。
- 结论：**可选（人工深挖）**。不进 LLM 自动化链。

#### B3 PIX / AMD RGP+RGD / Intel GPA
- PIX：D3D12 only，**不支持 Vulkan → 跳过**。
- RGP / RGD：AMD 专用 → **跳过**（本项目 NVIDIA）。
- Intel GPA：Intel → **跳过**。

---

### C 组 · 时间线与采样剖析（性能检测主线）

#### C1 Tracy Profiler（BSD-3）—— CPU+GPU 时间线主力
- 定位：实时 CPU/GPU 剖析；`ZoneScoped` 自动带函数名 + 文件 + 行号；`TracyVkCtx` 在 command buffer 里插 scope 得到**每 pass GPU 时间**。
- 触发方式：库内嵌（header-only + 源码）；服务端 `tracy-profiler`（GUI），**CLI 抓取用 `tracy-capture`**，**导出用 `tracy-csvexport`** → CSV 可被大模型读。
- 对本项目的贴合点：天然对齐 `IRenderPass` 架构——给每个 pass 包一层 GPU zone；CPU zone 可覆盖 update / culling / job system。
- 能力边界：需要 timestamp query 支持（2070 OK）；采集本身对 command buffer 有侵入。
- 集成成本：低-中。一个 header + 少量宏 + 一个 `TracyVkCtx`；编译期可整体开关。
- 风险：默认必须零开销；`--selftest` 无头模式下要能连到本地 server 抓一段固定时长。
- 结论：**采纳（P1 核心）**。P1-1。

#### C2 NVIDIA Nsight Systems（`nsys`）—— 系统级时间线，LLM 友好度最高
- 定位：CPU/GPU 全局时间线，Vulkan API 调用、kernel、拷贝、线程/队列并行度。
- 触发方式：`nsys profile --trace=vulkan,... -o out <exe> <args>`；`nsys stats out.nsys-rep`；**`--export sqlite`** → 大模型可直接 SQL 查询。
- 对本项目的贴合点：**双队列 + 2 帧在飞**的流水线重叠分析——看 graphics/compute 是否真的并行、CPU 是否被 fence/semaphore 阻塞。
- 集成成本：低（独立安装，主要写包装脚本）。
- 风险：注入层与验证层同时开时噪声变大，建议分开跑。
- 结论：**采纳（P2）**。

#### C3 Superluminal / Optick / Remotery
- Superluminal：Windows 采样强，但 **GUI-only + 商业授权** → LLM 工作流弱；**可选（人工）**。
- Optick / Remotery / microprofile：轻量可内嵌，但功能被 Tracy 覆盖 → **跳过**。

---

### D 组 · GPU 计数器 / 微架构

#### D1 NVIDIA Nsight Compute（`ncu`）
- 定位：逐 kernel 计数器（occupancy、内存带宽、warp stall）。
- 触发方式：`ncu --set full -o out <exe>`；`ncu -i out.ncu-rep --csv` → CSV/JSON 可读。
- 能力边界：**主要针对 compute / dispatch**，对纯 graphics pass 帮助有限。
- 对本项目的定位：为 **F1 GPU-driven / compute Bloom** 提前铺路，而非当前主线。
- 结论：**可选（P2，为 compute 铺路）**。

#### D2 AMD Radeon GPU Analyzer (RGA)
- 定位：离线着色器静态分析。AMD 为主，但对 SPIR-V 也有静态价值。
- 结论：**跳过**（已有 SPIRV-Tools 覆盖大部分静态需求）。

---

### E 组 · 静态与环境工具（**已装但未接入，最便宜的收益**）

实测 `D:\work\vulkan\Bin` 已有：`spirv-val / spirv-opt / spirv-dis / spirv-cross / spirv-reflect / spirv-cfg / spirv-diff / spirv-reduce / spirv-link / spirv-as / dxc / glslangValidator / vkconfig`。

#### E1 `spirv-val`
- 定位：SPIR-V 静态合法性校验。现在构建只跑了 `glslangValidator -V`，**缺少合法性校验**。
- 集成：CMake 里给每个 shader 加一步 `spirv-val`，等于把一部分验证前移到编译期。
- 结论：**采纳（P0，必做）**。

#### E2 `spirv-opt` / `spirv-dis` / `spirv-cross` / `spirv-reflect`
- 用途：优化与 strip debug；反汇编（让大模型读 SPIR-V 排查 layout/binding 不匹配）；反射（导出接口 JSON）。
- 结论：**可选（按需手动调用）**。

#### E3 `vulkaninfo`
- 定位：环境快照（GPU、驱动、支持的扩展/限制）。
- 集成：`vulkaninfo --summary` 落盘为 `env.txt`，解决「我机器能跑你机器不能」类问题。
- 结论：**采纳（P0）**。

#### E4 运行期设备与时间扩展
- `VK_EXT_device_fault`：device lost / 崩溃后取故障信息（`vkGetDeviceFaultInfoEXT`）。
- `VK_EXT_memory_budget`：内存预算（VMA 已用）。
- `VK_EXT_calibrated_timestamps`：精准 GPU 计时（P0-4 依赖）。
- 结论：**采纳（P0-4 / P2-4）**。

---

### F 组 · 自研基础设施（不是外部工具，但不可替代）

#### F1 GPU pass 计时（`--gpu-timing`）
- 定位：每个 pass 头尾 `vkCmdWriteTimestamp2`（配合 `VK_EXT_calibrated_timestamps`），退出时落 `gpu_timing.csv`。
- 价值：**常驻体检**——能在回归里发现「SSAO 从 0.3ms 涨到 2ms」这类 GPU 侧退化，外部 profiler 替代不了这种低成本常驻能力。
- 结论：**采纳（P0 核心）**。

#### F2 无头截图 + 视觉回归（`--screenshot`）
- 定位：headless 渲一帧写 PNG（用已有的 `stb_image` 写图），配参考图做像素 diff，输出差异分数 JSON。
- 附加价值：**大模型（若带图像读取能力）可以直接看 PNG**，所以截图 artifact 是有用的，不像纯 GUI 工具那样黑箱。
- 结论：**采纳（P1）**。

#### F3 调试包（debug bundle）
- 定位：把一次会话的所有证据（validation、gpu_timing、env、renderdoc 摘要、nsys stats、截图）聚合到一个带时间戳的目录 + `manifest.json`。
- 结论：**采纳（P3）**。

---

### G 组 · 厂商专用工具（可选补充，**不纳入默认路线**）

> **决策（2026-10-17）**：本项目的**默认**调试链路必须**厂商中立**，避免将来换显卡（如 AMD）后整套工具失效。
> 因此 G 组全部标为「可选」：需要时按**当前显卡厂商**自行安装，**不作为回归或验收的一部分**，
> 也不写进 `Validate.ps1`。

| 厂商 | 工具 | 用途 | 备注 |
|---|---|---|---|
| NVIDIA | **Nsight Systems (`nsys`)** | 系统级时间线、Vulkan API 追踪、`--export sqlite` | 队列重叠分析；需 NVIDIA 卡 |
| NVIDIA | **Nsight Compute (`ncu`)** | 逐 kernel 计数器（占用率 / 带宽 / stall） | 主要面向 compute；需 NVIDIA 卡 |
| NVIDIA | Nsight Graphics | GPU Trace / 帧调试器 | GUI 为主 |
| AMD | Radeon GPU Profiler (RGP) / Radeon GPU Detective (RGD) | 帧剖析 / 崩溃分析 | 需 AMD 卡 |
| AMD | Radeon GPU Analyzer (RGA) | 离线着色器分析 | — |
| Intel | Graphics Performance Analyzers (GPA / Frame Analyzer) | 帧剖析 | 需 Intel 显卡 |

**NVIDIA 工具的安装线索（仅备查，未纳入自动化）**：
- Nsight Compute：`winget install Nvidia.Nsight.Compute`（本机 winget 已确认可装的直链为 NVIDIA 官方 MSI）。
- Nsight Systems：winget **没有**该包；需从 NVIDIA 官网（`developer.nvidia.com/nsight-systems`）手动下载安装。
- 两者都需要管理员权限；`ncu` 在 Windows 上还需要放开 GPU 计数器访问。

**跨厂商替代（已实现，见 §3 A–F 与 P0/P1 落地记录）**：

| 能力 | 默认（厂商中立）实现 |
|---|---|
| 时间线 / 每 pass GPU 计时 | **Tracy**（`TracyVkCtx`）+ 自研 `--gpu-timing` |
| 帧捕获与逐帧分析 | **RenderDoc**（NV/AMD/Intel 通用）+ `tools/Capture.ps1` |
| 正确性 | **Khronos 验证层**：标准 / Best Practices / Sync / GPU-AV / Debug Printf |
| 着色器静态检查 | **SPIRV-Tools**（`spirv-val` 已接入构建）|
| 环境快照 | `vulkaninfo` |
| 视觉回归 | `--screenshot` + `tools/Screenshot.ps1`（含金标准 diff）|

即：默认链路已覆盖「时间线 / 帧捕获 / 正确性 / 着色器 / 视觉」五类，G 组仅是**额外深度**。

---

## 4. 问题 → 工具 决策矩阵（排障手册）

| 症状 | 首选 | 次选 | 入口 |
|---|---|---|---|
| 花屏 / 随机闪烁 | Sync Validation + RenderGraph 子资源校验 | RenderDoc 看 layout / 绑定 | `--validation-features sync` / `--capture-frame N` |
| 黑屏 / 某 pass 无输出 | 验证层 + RenderDoc event 浏览 | Debug Printf | 验证层 flag |
| 崩溃 / device lost | `VK_EXT_device_fault` + 验证层 | RenderDoc 最后一帧 | P2-4 |
| 着色器越界 / 读到垃圾 | GPU-AV | RenderDoc 资源查看 | `--validation-features gpuav` |
| 着色器数值不对 | Debug Printf | RenderDoc 像素历史 | `--validation-features printf` |
| 帧率低于预期 | `--gpu-timing` 看 pass 分布 | Tracy GPU zone | P0-4 / P1-1 |
| 怀疑 CPU 瓶颈 | Tracy CPU zone | `nsys` | P1-1 / P2-1 |
| GPU 空转 / 队列串行 | `nsys` 时间线 | Tracy | P2-1 |
| pipeline / descriptor 开销 | Best Practices | `nsys` API 统计 | `--validation-features bp` |
| compute kernel 慢 | `ncu` | Tracy GPU zone | P2-2 |
| 视觉回归 | `--screenshot` + golden diff | RenderDoc | P1-4 |
| 内存泄漏 | VMA 泄漏检查（已有） | `VK_EXT_memory_budget` | `-DSENTINEL_VMA_LEAK_CHECK=ON` |
| 环境差异 | `vulkaninfo` 快照 | | P0-5 |

---

## 5. 阶段计划

每个条目的「验收标准」都遵守项目现有规矩：**退出码 0 + stderr 0 字节 + 关键 stdout 标记**（除非该项本身就是「故意触发错误」的反例测试）。

### P0 · 零依赖、快速见效、先保基线（估算 0.5 ~ 1 天）

**目标**：在不引入任何外部安装的前提下，把 GPU 侧可观测性和验证层能力拉起来，并消除 stderr 隐患。

| 编号 | 事项 | 交付物 | 验收标准 | 风险 |
|---|---|---|---|---|
| **P0-1** | 验证层回调改造：结构化、可落盘、不污染 stderr | 回调改为写 stdout 标记 / `validation.jsonl`；进程内累计 error / warning 计数 | `Validate.ps1` 仍全绿、stderr 仍 0 字节；故意注入一个验证层错误时能被计数且**不写 stderr** | 需保证现有「全绿」不被打破；VERBOSE 噪声要过滤 |
| **P0-2** | 验证层能力开关：`--validation-features bp,sync` | 新 CLI flag + `VkValidationFeaturesEXT` 链入 | **默认关闭时行为与现状字节级一致**；开启时 `validation.jsonl` 出现对应类别消息 | syncval 可能噪声大；只作为「深度作业」，不进默认回归 |
| **P0-3** | `spirv-val` 接入构建 | CMake 中每个 shader 增加校验步骤 | 非法 SPIR-V 能让构建失败；正常 shader 构建仍通过 | 校验失败信息要清晰 |
| **P0-4** | `--gpu-timing` + `gpu_timing.csv` | 每 pass timestamp + CSV 落盘 | 关闭时**无额外 query**；开启时 CSV 数值非零、随 pass 变化、可复现 | 需要正确处理 2 帧在飞的 query pool 复用与 readback |
| **P0-5** | `tools/Profile.ps1` 雏形 + `vulkaninfo` 快照 | 脚本产出 `build/profiles/<timestamp>/`（含 `env.txt`、`gpu_timing.csv`） | 一条命令产出目录；符合 stderr 基线 | — |
| **P0-6** | 零开销守卫 + `--build-info` | 自计数 query pool / 分配；启动打印「编译进的」与「本次开启的」调试能力 | 默认运行新增分配 / 命令 / 文件均为 0；`--gpu-timing` 关闭时不创建任何 timestamp query | 计数点须与实际创建路径同源，避免自证失真 |

**P0 完成后的最小「调试语境」**：验证层消息（结构化）+ 每 pass GPU 时间 + 环境快照。

**P0 落地记录（2026-10-17）**：
- 新增 `src/Core/Diagnostics.*`（运行期选项 / 验证层结构化计数 + JSONL / `--build-info` / 诊断自检）与 `src/RHI/GpuTimer.*`（每 pass 时间戳）。
- `RenderGraph` 增加 GPU 计时挂钩（`SENTINEL_ENABLE_GPU_TIMING` 关闭时零分支）；`Device` 验证层回调改写为结构化、不再写 stderr，并支持 `VkValidationFeaturesEXT`。
- CMake 新增 `SENTINEL_ENABLE_GPU_TIMING / VALIDATION_FEATURES / BUILD_INFO / PROFILER`；构建期对每个 shader 追加 `spirv-val`。
- 新增 `tools/Profile.ps1`（调试包）与 3 项 P0 回归；`Validate.ps1` 追加全局「零验证层消息」断言。

### P1 · 帧捕获与时间线（估算 2 ~ 4 天）

| 编号 | 事项 | 交付物 | 验收标准 |
|---|---|---|---|
| **P1-1** | Tracy 集成 | `TracyVkCtx` + 每 pass GPU zone + 关键 CPU zone；编译期开关 | 关闭时零开销；开启时 `tracy-capture` 能抓固定时长并 `tracy-csvexport` 出 CSV |
| **P1-2** | RenderDoc 集成 | 引擎 `--capture-frame N`（in-app API）+ `tools/Capture.ps1` + python 摘要导出 | 能无人值守抓第 N 帧并产出 `frame_N.json` 摘要 |
| **P1-3** | Debug Printf 开关 | `--validation-features printf` + 一个示例着色器 | 着色器里的 `printf` 能出现在结构化日志中 |
| **P1-4** | 无头截图 + 视觉回归 | `--screenshot out.png` + golden diff 脚本 + 差异 JSON | 两帧相同场景差异分数≈0；改动明显时分数显著上升 |

**P1 落地记录（2026-10-17）**：
- 新增 CMake 开关 `SENTINEL_ENABLE_SCREENSHOT`(ON) / `SENTINEL_ENABLE_TRACY`(OFF) / `SENTINEL_ENABLE_RENDERDOC`(OFF)；缺失外部依赖时 CMake 自动禁用。
- P1-4：`src/RHI/ScreenshotCapture.*` + `--screenshot` / `--screenshot-frame` + `tools/Screenshot.ps1`（含金标准 diff）+ Validate 用例 `p1-screenshot`。
- P1-3：`--validation-features printf` 接入 `VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT`。
- P1-1：`src/Core/Profiler.h`（CPU zone / 帧标记，宏关闭时展开为空）；GPU 每 pass zone 待 Tracy 就位后在 RenderGraph 补。
- P1-2：`src/RHI/RenderDocHook.*` + `--capture-frame` + `tools/Capture.ps1`（未注入时为空操作）。

### P2 · 深度性能剖析（估算 1 ~ 3 天，按需）

| 编号 | 事项 | 交付物 | 验收标准 |
|---|---|---|---|
| ~~**P2-1**~~ | ~~`nsys` 接入~~ **可选（NVIDIA 专用，见 §3 G 组）** | 包装脚本 + `stats` 文本/CSV + `sqlite` 导出 | 不纳入默认路线 |
| ~~**P2-2**~~ | ~~`ncu` 接入~~ **可选（NVIDIA 专用，见 §3 G 组）** | 包装脚本 | 不纳入默认路线 |
| **P2-3** ✅ | GPU-AV / syncval 深度回归 | `--validation-features gpuav` + `tools/DeepValidation.ps1` + `summary.json` | 默认回归不变；深度作业单独运行并给出报告（已实施）|
| **P2-4** ✅ | `VK_EXT_device_fault` | 设备支持时启用；lost 时 `vkGetDeviceFaultInfoEXT` 输出故障；`--device-fault-test` 自检 | 能力就绪自检通过；查询路径挂在 device-lost 分支（已实施）|

### P3 · 调试包与自动化封装（估算 1 ~ 2 天）

| 编号 | 事项 | 交付物 | 验收标准 |
|---|---|---|---|
| **P3-1** ✅ | 调试包整合 | `tools/DebugBundle.ps1` + `manifest.json` / `summary.md` 聚合全部 artifact | 一个目录即含全部证据并带每步状态（已实施）|
| **P3-2** ✅ | 基线与 diff 机制 | `tools/Baseline.ps1`（Save/Diff）+ `baseline.json` / `diff.json` | 三维度（GPU 时间 / 验证层 / 视觉）自动判回归（已实施）|
| **P3-3** | （可选进阶）MCP 封装 | 把 RenderDoc Python / nsys sqlite 查询封成工具接口 | 大模型可直接查询 drawcall / 时间线（自研工作，方向性探索） |

---

## 6. Artifact 规格（调试包）

### 6.1 目录布局

```text
build/
└── profiles/
    └── <timestamp>/           # yyyyMMdd-HHmmss
        ├── manifest.json      # 本次会话的索引与元数据
        ├── env.txt            # vulkaninfo --summary
        ├── validation.jsonl   # 验证层结构化消息（一行一条）
        ├── gpu_timing.csv     # 每 pass GPU 时间
        ├── frame_<N>.rdc      # （可选）RenderDoc 捕获
        ├── frame_<N>.json     # （可选）捕获摘要（python 导出）
        ├── screenshot_<N>.png # （可选）无头截图
        ├── visual_diff.json   # （可选）视觉回归分数
        └── nsys/              # （可选）nsys stats / sqlite
```

### 6.2 `validation.jsonl`（每行一个 JSON 对象）

```json
{"ts":"2026-10-17T12:00:00.123","severity":"ERROR","type":"VALIDATION","id":"VUID-VkImageCreateInfo-...","frame":42,"msg":"..."}
```

- `severity`：`VERBOSE|INFO|WARNING|ERROR`
- `type`：`GENERAL|VALIDATION|PERFORMANCE`
- `id`：VUID（如有）
- `frame`：可选，若能从上下文推断
- **约定**：任何 ERROR/WARNING 同时累计到 `manifest.json.validationCounts`，供快速判定。

### 6.3 `gpu_timing.csv`

```csv
frame,pass,gpu_ms
120,Geometry,0.42
120,SSAO,0.31
120,Lighting,1.87
120,Bloom,0.95
120,Present,0.20
```

- 只在 `--gpu-timing` 下产出；帧号可只在稳定帧采样（例如跳过前 K 帧预热）。
- 建议同时输出 `gpu_timing_summary.csv`（每 pass 的均值 / P95）。

### 6.4 `manifest.json`（索引）

```json
{
  "timestamp": "2026-10-17T12:00:00",
  "engineCommit": "no-git",
  "buildConfig": "Debug",
  "vulkanSdk": "D:\\work\\vulkan",
  "gpu": "NVIDIA GeForce RTX 2070",
  "frames": 120,
  "renderScale": 1.0,
  "bloom": true,
  "ssao": true,
  "validationCounts": { "error": 0, "warning": 0 },
  "artifacts": ["env.txt", "validation.jsonl", "gpu_timing.csv"]
}
```

---

## 7. LLM 调试工作流（未来会话怎么用）

推荐固定流程：

1. **确认环境**：读 `PROJECT_STATE.md`；跑 `tools/Validate.ps1`（或 `-Incremental -SkipSetup -Frames 20`）确认基线。
2. **复现**：用最小 `--selftest N`（加上相关 flag）复现问题，确认退出码与 stderr。
3. **抓证据**：跑 `tools/Profile.ps1` 生成调试包；按 §4 矩阵选工具补充证据。
4. **定位**：按「validation → gpu_timing → 帧捕获 / 时间线 → 着色器」顺序逐层排查，每层结论落到文件。
5. **修复**：最小改动，保持既有工程约定。
6. **回归**：`Validate.ps1` 全绿 + stderr 0 字节；若有性能目标，比对 `gpu_timing_summary.csv`。
7. **存档基线**：必要时更新「已知良好」基线目录，供后续 diff。

---

## 8. 兼容性与风险清单（针对本项目特性）

| 项目特性 | 潜在冲突 | 缓解 |
|---|---|---|
| `descriptorBindingSampledImageUpdateAfterBind`（bindless） | GPU-AV 兼容性、RenderDoc descriptor 视图受限 | GPU-AV 手动开；RenderDoc 用 python 摘要而非 GUI |
| `kFramesInFlight = 2` + timeline semaphore | pass 计时的 query pool 复用、readback 时序 | 每 frame slot 独立 query pool；仅在安全帧 readback |
| 自动 barrier 的 RenderGraph | syncval 可能噪声大 | syncval 只进「深度作业」 |
| Dynamic rendering（1.3 核心） | 部分工具的 render pass 分析模型不同 | 用较新版工具；必要时改用捕获事件流分析 |
| 动态分辨率 / resize | 截图与视觉回归的分辨率一致性 | 截图固定 `renderScale` 与尺寸基线 |
| graphics/compute 双队列 + CONCURRENT sharing | 注入层与验证层同时开噪声大 | 分开跑：先验证层，后 nsys |
| 无 git | 无法 diff 代码 | 用带时间戳的 artifact + 基线目录 |
| stderr 0 字节基线 | 任何工具默认写 stderr 都会破坏 | 所有日志走 stdout 标记或文件 |

---

## 9. 决策记录与待定问题

**已定**：
- 采纳：验证层增强（P0-1/P0-2）、`spirv-val`（P0-3）、`--gpu-timing`（P0-4）、Tracy（P1-1）、RenderDoc（P1-2）、Debug Printf（P1-3）、`--screenshot`（P1-4）、`nsys`（P2-1）、`ncu`（P2-2）、调试包（P3-1/P3-2）。
- 可选：Best Practices、syncval、GPU-AV、Nsight Graphics、Superluminal、RGA、MCP 封装。
- 跳过：PIX、AMD RGP/RGD、Intel GPA、Optick/Remotery、Shader Instrumentation（单独）。
- **零开销口径（2026-10-17 定）**：不追求绝对零；判据为「明显影响渲染速度的绝对不能留」。允许每帧每 pass 一处 hoisted 判断；重量级能力编译期关闭；**不引入** `SENTINEL_ZERO_OVERHEAD`，调试能力走「同一 build + 运行期开关」。
- **厂商中立（2026-10-17 定）**：默认调试链路只采用厂商中立工具；NVIDIA/AMD/Intel 专用工具全部降级为「可选补充」（§3 G 组），不进回归、不进验收。

**待定**：
1. Tracy 是否随引擎源码内嵌（`3rdParty/tracy`）——需在 `SetupDependencies.ps1` 固定版本，且不入库。
2. GPU-AV 与 bindless 的实机兼容性——需一次专项验证后才能确认是否纳入深度作业。
3. `--gpu-timing` 的采样策略（每帧 vs 抽样）与精度（单次 vs 多次平均）。
4. 视觉回归是否引入固定金标准图（golden image）——涉及资源准备与维护成本。
5. 深度作业（syncval / GPU-AV / nsys）是否单独出一个 `tools/DeepProfile.ps1`。
6. ~~syncval / best-practices 告警~~ **已排查（2026-10-17）**：syncval 的 hazard 是真实（虽实践中良性）的同步语义缺口——`LOAD` 读访问位缺失 + acquire 等待阶段过窄——已修，现 **0 errors**；best-practices 余 13 条 warning 均为良性优化提示（SPIR-V `WorkgroupSize` 在 1.6 弃用、VMA 专用块绑定），低优先级。

---

## 10. 附录 · 命令速查（规划草稿，实施前需按版本核对）

```powershell
# 环境快照
vulkaninfo --summary > env.txt

# 着色器校验（SDK 已带）
spirv-val shaders/foo.frag.spv
spirv-dis build/shaders/foo.frag.spv | more

# RenderDoc 无人值守抓帧（草稿）
renderdoccmd capture -c frame_120.rdc --wait-for-exit --opt-hook-children `
    .\build\Debug\VulkanEngine.exe --selftest 120

# 引擎自研入口（规划）
.\build\Debug\VulkanEngine.exe --selftest 120 --gpu-timing
.\build\Debug\VulkanEngine.exe --selftest 120 --capture-frame 120
.\build\Debug\VulkanEngine.exe --selftest 120 --screenshot shot.png
.\build\Debug\VulkanEngine.exe --selftest 120 --validation-features bp,sync

# Nsight Systems（需单独安装）
nsys profile --trace=vulkan,nvtx,osrt -o report .\build\Debug\VulkanEngine.exe --selftest 120
nsys stats report.nsys-rep --format csv

# Tracy CLI（需单独安装 Tracy 发行包）
tracy-capture -o session.tracy -s 30
tracy-csvexport session.tracy > zones.csv

# 验证层深度开关（备用：也可用 vkconfig 生成 layer settings JSON）
#   通过 VkValidationFeaturesEXT 或 vkconfig 启用 best practices / sync / gpuav / printf
```

---

## 附：与现有文档的关系

- `PROJECT_STATE.md`：项目现状与接手说明（本文应在其实施后回填进度）。
- `TECH_DEBT.md`：本计划的各项可作为「可观测性」性质的条目登记。
- `FUTURE_PLAN.md`：本计划是 F1/F2/F3 之前的「基础设施前置」。
- `tools/README.md`：新增 `Profile.ps1` / `Capture.ps1` 后需在此登记。