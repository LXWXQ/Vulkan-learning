# Sentinel Vulkan Engine - 项目现状与接手说明

> 更新时间：2026-10-17
>
> 用途：让下一个对话在最少上下文下快速接手。先读本文，再按需要深入 `AI_Readme.md`、`ARCHITECTURE.md`、`FUTURE_PLAN.md`。
>
> 当前工作区没有 `.git` 元数据，不能依赖 `git log` / `git status` 判断状态。以文件系统实际内容为准。

## 0. 30 秒结论

- 这是一个 Windows + Vulkan 1.3 的 C++17 学习/工程验证型渲染器。
- 渲染主线：G-Buffer -> SSAO -> Lighting HDR -> Bloom -> Present。
- 已实现：线性 RenderGraph、自动 barrier、子资源校验、bindless 材质、2 帧在飞、graphics/compute 双队列、IBL、EnTT ECS、JobSystem、窗口缩放、动态分辨率、版本化配置。
- 技术债 TD-01 ~ TD-09 已全部完成。
- 当前默认渲染路径**不再运行 compute probe**；只有 `--compute-test` 会启用。
- **调试 / 剖析能力随用随开**：默认关闭，关闭后不影响运行时性能；明显拖慢渲染的调试代码绝不能留在默认路径（详见 `DEBUG_PROFILE_PLAN.md`）。
- **P0 基础设施已落地**：`--build-info` / `--gpu-timing`(+CSV) / `--validation-features` / `--validation-jsonl` / `--diagnostics-test`；验证层回调不再写 stderr；`tools/Profile.ps1` 一键产出调试包。
- 项目已清理：`build/`、`imgui.ini`、废弃 shader、源码目录 `.spv` 均已删除。
- 最后一次完整验证（2026-10-17，含 P0）：**16 项测试全部通过**，所有测试 stderr 为 0 字节。

## 1. 接手后的第一步

从仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1
```

它会：

1. 检查/下载固定版本的第三方依赖；
2. 执行 TD-08 dead-file guard；
3. 全新 configure + Debug build；
4. 运行全部回归；
5. 检查退出码、stderr 字节数和 stdout 标记。

日志在：

```text
build/validation/<timestamp>/
```

因为清理时删除了 `build/`，第一次运行会重新生成约 100 MB 的构建目录，这是正常的。

如果 Vulkan SDK 未自动找到：

```powershell
$env:VULKAN_SDK = "D:\work\vulkan"
```

当前机器上的 SDK 通常是 `D:\work\vulkan`。

## 2. 项目定位

这个项目优先解决的是渲染工程正确性，而不是功能数量：

- Pass 只声明资源访问，RenderGraph 自动推导图像/缓冲区屏障。
- 跨帧资源按 frame slot 隔离，避免 CPU/GPU 重叠时互相覆盖。
- 所有关键能力都有命令行自检或正向/反例回归。
- 技术债修复必须以 stderr 0 字节和可复现命令收尾。

当前渲染输出为线性 HDR 中间目标，PresentPass 负责曝光、ACES tonemap、gamma 和 ImGui 叠加。

## 3. 当前已实现能力

### 3.1 RenderGraph

- 线性 pass 顺序，不做重排、自动剔除或内存别名。
- 自动 `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2`。
- `TextureHandle` / `BufferHandle` 生命周期由调用方或 RenderGraph 管理。
- 瞬时资源写覆盖校验。
- `SubresourceRange`：按 mip/layer 独立跟踪 layout/stage/access。
- `BufferRange`：按 offset/size 跟踪写覆盖。
- graphics/compute 跨队列 producer-consumer 单向依赖推导与环检测。
- **同步语义补全（2026-10-17）**：`writeColor`/`writeDepth` 在 `LOAD` 语义下补上读访问位（否则 syncval 报 `READ_AFTER_WRITE`）；graphics 等 `imageAvailable` 的阶段放宽到 `ALL_COMMANDS`（避免与 acquire 的 `WRITE_AFTER_READ`）。修复后 `--validation-features sync` 报 **0 errors / 0 warnings**。

### 3.2 多帧与队列

- `kFramesInFlight = 2`。
- 每个 frame slot 有独立 graphics/compute 命令缓冲、timeline semaphore 和瞬时资源。
- graphics 与 compute 有独立队列；当前机器上 compute family 是独立 family。
- 跨队列资源走 `VK_SHARING_MODE_CONCURRENT` 简单路径。
- 依赖方向决定 submit 顺序；双向依赖会明确报错。

### 3.3 材质

- 单个 bindless 材质描述符 set。
- 4 个纹理数组：albedo / normal / metallic / roughness。
- 材质切换只改 push constant 中的 materialId。
- 启用 `descriptorBindingSampledImageUpdateAfterBind`。
- 槽位归还不调用 `vkDeviceWaitIdle`，而是进入 graphics timeline 延迟回收队列。
- 分配/归还压力测试：`--material-stress`。

### 3.4 后效链

- Bloom 开启：Bright 提取 + 3 级 Down + 3 级 Up。
- Bloom 关闭：7 个 Bloom Pass 全部跳过，Present 使用单输入 `present_no_bloom.frag`。
- Present 顺序：HDR + Bloom -> exposure -> ACES Filmic -> gamma -> ImGui。

### 3.5 窗口与分辨率

- 窗口可缩放。
- 在帧边界等待 GPU 后原地重建 swapchain、render-finished semaphore 和 ImGui 后端。
- 中间目标按 pass 顺序 resize：Geometry -> SSAO -> Lighting -> Bloom -> Present。
- 动态分辨率只缩放 G-Buffer/SSAO/HDR/Bloom，不重建管线，也不改变 swapchain。

### 3.6 场景与 CPU

- EnTT ECS。
- Transform / WorldTransform 层级传播。
- 均匀网格 SpatialIndex broad-phase + frustum culling。
- JobSystem worker pool。
- glTF / OBJ 加载与材质槽位分配。

### 3.7 配置

- `config/engine.json` 当前 `version = 2`。
- 显式迁移：v0 -> v1 -> v2。
- 迁移前生成 `.vN.bak` 备份。
- 损坏 JSON、非法版本、未来版本都不覆盖原文件。
- `save()` 对不可读的已有配置直接拒绝覆盖。

### 3.8 调试与剖析基础设施（P0–P3）

- **开关模型**：能力「随用随开」，默认关、关闭零开销。是否「编译进来」由 CMake 的 `SENTINEL_ENABLE_*` 选项决定；是否「本次开启」由命令行 flag 决定（详见 `DEBUG_PROFILE_PLAN.md` §1.5）。
- **验证层回调不再写 stderr**：改为在 diag 模块累计 error/warning 计数，可选 JSONL 落盘（`--validation-jsonl`），退出时打印 `[Validation] errors=N warnings=M`。
- **`--gpu-timing`**：每 pass `vkCmdWriteTimestamp2`（`VK_EXT_calibrated_timestamps` 的 `timestampPeriod`），退出写 `gpu_timing.csv`；关闭时不创建任何 query pool。
- **`--validation-features bp,sync`**：可选启用验证层 best practices / 同步校验。
- **`--build-info`**：自报「编译进的」与「本次开启的」能力清单。
- **`--diagnostics-test`**：诊断管线自检（合成消息 -> 计数 + JSONL），不依赖 Vulkan。
- **`tools/Profile.ps1`**：一次调用产出 `build/profiles/<timestamp>/`（`env.txt` + `gpu_timing.csv` + 运行日志 + `manifest.json`）。
- **`--screenshot <path>`（P1-4）**：目标帧把交换链图像读回并写 PNG；`--screenshot-frame <n>` 指定帧。交换链图像在此模式下额外带 `TRANSFER_SRC` usage。
- **`--capture-frame <n>`（P1-2）**：RenderDoc in-app 抓帧（未注入 RenderDoc 时为空操作）。
- **Tracy 挂钩（P1-1）**：`src/Core/Profiler.h`（CPU zone/帧标记）+ `src/RHI/GpuProfiler.*`（每 pass GPU zone）+ RenderGraph 集成；`SENTINEL_ENABLE_TRACY=OFF`（默认）时全部展开为空。**ON 路径已验证**。
- **`tools/Screenshot.ps1` / `tools/Capture.ps1`**：截图 + 金标准 diff；RenderDoc CLI 抓帧包装。
- **P2-3 深度校验作业（厂商中立）**：`--validation-features gpuav` 接入 `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT`；`tools/DeepValidation.ps1` 一次性跑 sync / bp / gpuav / printf，输出 `summary.json`，**不进默认回归**。
- **P2-4 `VK_EXT_device_fault`**：设备支持时启用；`Renderer` 在 `VK_ERROR_DEVICE_LOST` 时调 `vkGetDeviceFaultInfoEXT` 输出 `[DeviceFault]` 故障信息；`--device-fault-test` 校验能力就绪。
- **P3-2 基线 + diff（厂商中立）**：`tools/Baseline.ps1 -Action Save/Diff` 一次采集 GPU 时间 / 验证层 / 视觉三维度并自动判回归；剔除预热帧，阈值可配；`Validate.ps1` 末尾跑 `p3-baseline-diff` 自测。
- **P3-1 一键证据包**：`tools/DebugBundle.ps1` 产出 `build/bundles/<ts>/`，含 `manifest.json` / `summary.md` / `build_info.txt` / `env.txt` / `run/` / `validation/` / `deep/` / `baseline_diff/`；每步记录状态，失败则退出码非 0。`Profile.ps1` 是其轻量版。

---

## 4. 构建与运行

### 4.1 手动构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/SetupDependencies.ps1

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

.\build\Debug\VulkanEngine.exe
```

### 4.2 常用命令行参数

| 参数 | 说明 |
|---|---|
| 无参数 | 正常窗口运行 |
| `--selftest N` | 跑 N 帧后退出 |
| `--no-bloom` | 关闭 Bloom |
| `--no-ssao` | 关闭 SSAO |
| `--render-scale F` | 动态分辨率倍率，0.25~1.0 |
| `--resize-test` | 连续缩放窗口、切换分辨率、最小化/恢复 |
| `--jobtest` | 仅测试 JobSystem，不创建 Vulkan 窗口 |
| `--compute-test` | 启用 compute probe 的 GPU 写 + CPU 读回 |
| `--subresource-test` | mip/layer 与 buffer 区间正向探针 |
| `--subresource-error-test` | 故意写 mip0 后读 mip1，期望被拒绝 |
| `--material-stress` | 反复分配/延迟归还材质槽位 |
| `--config-test` | 配置迁移、备份、损坏文件保护自检 |
| `--build-info` | 打印调试能力清单后退出（不创建窗口） |
| `--gpu-timing` | 每 pass GPU 计时，退出写 `gpu_timing.csv` |
| `--gpu-timing-csv <path>` | 指定 CSV 路径（隐含 `--gpu-timing`） |
| `--validation-features bp,sync` | 启用验证层 best practices / 同步校验 |
| `--validation-jsonl <path>` | 验证层消息写 JSONL（默认不落盘） |
| `--diagnostics-test [path]` | 诊断管线自检（合成消息） |
| `--screenshot <path>` | 目标帧截图写 PNG（+ `--screenshot-frame <n>`）|
| `--capture-frame <n>` | RenderDoc 第 n 帧抓帧（需注入 RenderDoc）|
| `--validation-features gpuav` | 启用 GPU-AV（着色器越界，独立于 all）|
| `--device-fault-test` | `VK_EXT_device_fault` 能力自检 |

### 4.3 推荐的单项回归

```powershell
.\build\Debug\VulkanEngine.exe --jobtest
.\build\Debug\VulkanEngine.exe --config-test
.\build\Debug\VulkanEngine.exe --selftest 120
.\build\Debug\VulkanEngine.exe --selftest 120 --no-bloom
.\build\Debug\VulkanEngine.exe --selftest 120 --no-ssao
.\build\Debug\VulkanEngine.exe --selftest 120 --no-bloom --no-ssao
.\build\Debug\VulkanEngine.exe --selftest 20 --compute-test
.\build\Debug\VulkanEngine.exe --selftest 60 --resize-test
```

正常回归要求：退出码 0，stderr 0 字节；并额外要求 stdout 出现 `[Validation] errors=0 warnings=0`（所有会创建 Vulkan 设备的用例）。

## 5. 验证矩阵

`tools/Validate.ps1` 默认跑 **18 项引擎用例** + 2 项工具级自测（`p3-baseline-diff`、`p3-debug-bundle`）；加 `-EnableProfiling` 时引擎用例 **19 项**：

| 测试 | 目的 |
|---|---|
| `jobtest` | CPU worker pool、parallelFor、异常传播 |
| `td09-config-migration` | v0->v2 迁移、备份、损坏/未来版本保护 |
| 四种 selftest | 默认 / no-bloom / no-ssao / both off |
| `td02-cross-queue` | C->G 与 G->C 跨队列探针 |
| `td07-compute-probe` | 独立 compute GPU 写 + CPU 读回 |
| `td03-window-resize` | 窗口缩放、动态分辨率、最小化/恢复 |
| `td03-render-scale` | 启动即 0.5x 中间目标 |
| `td04-material-slot-stress` | 150 轮材质槽位延迟回收 |
| `td05-subresource-probe` | mip/layer + buffer 区间正向探针 |
| `td05-subresource-error` | 子资源写覆盖反例 |
| `p0-build-info` | `--build-info` 能力清单 |
| `p0-diagnostics-selftest` | 诊断管线自检（合成消息 -> 计数 + JSONL） |
| `p0-gpu-timing` | 每 pass GPU 计时 -> `gpu_timing.csv` |
| `p1-screenshot` | 无头截图 -> PNG |
| `p2-device-fault` | `VK_EXT_device_fault` 能力就绪自检 |

最近一次实测性能摘要：

- Bloom 开：约 455 FPS
- Bloom 关：约 531 FPS
- compute probe 默认关：约 486 FPS
- compute probe 开：约 455 FPS

以上均为同机 Debug、3000 帧自检数据，只适合相对比较，不是跨机器基准。

## 6. 代码结构与职责

```text
src/
├── Core/
│   ├── Application.*        # 引擎总控、窗口、初始化顺序、帧循环、resize
│   ├── DescriptorManager.*  # 全局 set + bindless 材质槽位 + 延迟回收
│   ├── EngineConfig.*       # version 2 配置、迁移、备份、自检
│   ├── JobSystem.*          # CPU worker pool / submit / parallelFor
│   ├── ImGuiSystem.*        # ImGui 后端与面板
│   └── FrameInfo.h          # 每帧数据总线、GlobalUbo、EngineSettings
├── Render/
│   ├── IRenderPass.h        # Pass 抽象
│   ├── RenderPipeline.h     # Pass 注册与 resize
│   ├── RenderResources.h    # 帧内资源登记表
│   ├── GeometryPass.*       # 导入 G-Buffer、几何绘制
│   ├── SSAOPass.*           # SSAO 输出
│   ├── LightingPass.*       # HDR 光照输出、导入交换链图像
│   ├── BloomPass.*          # Bloom 金字塔，关闭时直接跳过
│   ├── PresentPass.*        # Bloom/NoBloom 双路径，ACES + ImGui
│   ├── IBLBaker.*           # 一次性 GPU IBL 烘焙
│   └── *ProbePass.*         # compute/cross-queue/subresource 测试探针
├── RHI/
│   ├── Device.*             # instance/device/queues/features/VMA
│   ├── Renderer.*           # frame slot、timeline semaphore、submit/present
│   ├── RenderGraph.*        # 屏障、pass 录制、子资源校验
│   ├── Swapchain.*          # 交换链与原地 recreate
│   ├── GBuffer.*            # 4 色 + depth
│   ├── Pipeline.*           # Graphics / Compute pipeline 封装
│   ├── Texture.*            # 普通纹理上传
│   └── CubemapTexture.*     # cubemap 与逐 mip/face view
└── Scene/
    ├── Scene.*              # EnTT registry、场景装配、可见性
    ├── GLTFLoader.*         # glTF 导入
    ├── Model.*              # 顶点/索引上传
    ├── SpatialIndex.*       # 均匀网格
    └── Camera* / Components.h
```

## 7. 必须保持的工程约定

1. **新增 `.cpp` 后重新 configure**
   - CMake 已使用 `CONFIGURE_DEPENDS`，但全新验证脚本仍会主动 configure。

2. **Pass 必须如实声明资源访问**
   - 漏声明不会一定报错，可能只表现为随机花屏或验证层 layout 错误。
   - 瞬时资源每帧必须有人写；子资源要写清 mip/layer。

3. **跨队列资源必须设置 `crossQueue=true`**
   - 当前简单路径是 `VK_SHARING_MODE_CONCURRENT`。
   - 同一帧只允许单向依赖；双向依赖会抛异常。

4. **材质槽位不能立即复用**
   - `releaseMaterialSet()` 只入延迟队列。
   - 只有 graphics timeline 确认旧提交退休后才回 freeSlots。

5. **渲染输出与窗口尺寸解耦**
   - 交换链是窗口尺寸。
   - G-Buffer/SSAO/HDR/Bloom 由 `renderScale` 决定。
   - 管线使用动态 viewport/scissor，不按分辨率重建。

6. **配置加载失败不能覆盖原文件**
   - 损坏、非法版本、未来版本都必须保留原文件。
   - 不要把 `save()` 改成解析失败就重建默认配置并写回。

7. **验证以 stderr 字节数为准**
   - 任何预期错误都应被测试捕获，不能让 `std::cerr` 污染 stderr。
   - 反例测试应在进程内捕获异常并输出 PASS 标记。

8. **调试 / 剖析能力一律「随用随开」，关闭后不得影响运行时性能**
   - 默认关闭；开启方式为正着命名的 CLI flag（如 `--gpu-timing`），或编译期宏 / CMake option。
   - 关闭时不得做任何分配、不得往 command buffer 插命令、不得建 query pool / hook / 线程。
   - 重量级或带外部依赖的能力（Tracy / RenderDoc hook / Debug Printf / GPU-AV）默认**编译期关闭**。
   - 判据不是「绝对零开销」，而是**「明显影响渲染速度的绝对不能留」**：允许每帧每 pass 一处的提前判断，禁止任何可测量的渲染性能回退。
   - 完整调研与阶段计划见 `DEBUG_PROFILE_PLAN.md`。

## 8. 已知限制

- 跨队列资源当前使用 CONCURRENT sharing，尚未实现 EXCLUSIVE queue-family ownership transfer。
- glTF 内部节点层级在导入时烘焙到世界空间，Sponza 内部节点不是独立动态 entity。
- SpatialIndex 每帧重建，尚无 dirty / 增量更新。
- 尚未实现阴影、SSR、自动曝光、动画、音频、编辑器。
- Tracy / RenderDoc 依赖已由 `tools/SetupProfiling.ps1` 下载到 `3rdParty`，P1-1/P1-2 的 ON 路径已验证（`Validate.ps1 -EnableProfiling` 19 项全绿）；默认仍是编译期关闭。nsys/ncu 为厂商专用，已降级为可选补充（见 `DEBUG_PROFILE_PLAN.md` §3 G 组），未安装。
- `VK_EXT_device_fault` 的查询**只能在设备已 lost 时调用**（VUID-07336），所以健康设备上只能校验能力就绪，真实故障路径由 Renderer 的 device-lost 分支覆盖。
- ComputeProbe / CrossQueueProbe / SubresourceProbe 是测试探针，不是生产渲染任务。
- `Resources/` 体积较大且不入库，需要单独准备；`3rdParty/` 由 setup 脚本准备。
- 当前工作区没有 git 仓库，不能靠版本控制判断未提交改动。

## 9. 下一步方向

技术债已经清空，后续按 `FUTURE_PLAN.md` 走。推荐从以下方向中选一个，不要同时铺开：

1. **F1 GPU-driven rendering**
   - 需要 compute 生成的 indirect draw / instance buffer。
   - 当前跨队列 producer-consumer 基础设施已具备。
2. **F2 地形系统**
   - 需要 chunk 化、LOD、GPU 或 JobSystem 生成。
   - SpatialIndex 的增量更新会成为前置。
3. **F3 体素系统**
   - 需要 chunk 存储、网格生成、compute/JobSystem 接入。
4. **F4 资产与场景管线**
   - glTF 内部 node 层级、材质流送、场景资源状态恢复。
5. **渲染特性**
   - 阴影、SSR、自动曝光、compute Bloom。

开始任何主功能前，先确认：

- 对应前置技术债是否真的完成。
- 是否需要扩展 RenderGraph 的队列/子资源能力。
- 是否需要新的验证入口和反例。
- 不要为了功能把现有 stderr 0 字节的验证基线破坏。

## 10. 文档索引

| 文档 | 用途 |
|---|---|
| `README.md` | 项目入口、构建、命令行、目录概览 |
| `PROJECT_STATE.md` | 当前现状与接手说明（本文） |
| `AI_Readme.md` | 逐系统深入手册、历史坑、验证工作流 |
| `ARCHITECTURE.md` | 架构决策、资源生命周期、设计取舍 |
| `TECH_DEBT.md` | 技术债历史与完成状态 |
| `FUTURE_PLAN.md` | 后续主功能路线与验收标准 |
| `HANDOFF.md` | 更长的历史交接记录 |
| `tools/README.md` | 依赖固定版本与验证脚本说明 |
| `DEBUG_PROFILE_PLAN.md` | 调试 / 性能剖析工具链、阶段计划、零开销开关约定与 LLM 排障手册 |

## 11. 给下一个对话的直接提示

- 先读本文，不要从旧 `HANDOFF.md` 的 D-4/D-5 状态开始推断。
- 先跑 `tools/Validate.ps1` 或至少 `--selftest 20`，确认当前环境。
- 默认 render path 不包含 compute probe；需要 D-5 回归时加 `--compute-test`。
- Bloom 开关现在会真正增删 7 个 pass；不要再恢复强度置零照跑的旧实现。
- 改材质槽位生命周期时保留 update-after-bind + timeline 延迟回收。
- 改 RenderGraph 时保留 mip/layer 与 buffer 区间语义校验。
- 改 EngineConfig 时保留 version 2、迁移备份和损坏文件保护。
- 任何新增测试仍以：退出码 0、stderr 0 字节、关键 stdout 标记为验收标准。
- 引入任何调试 / 剖析工具前先读 `DEBUG_PROFILE_PLAN.md`：一律「随用随开」，关闭后不得影响运行时性能。
- 新增 / 修改调试能力时保持「`SENTINEL_ENABLE_*` 编译期开关 + 命令行 flag」两层模型；`--build-info` 用于确认二进制实际编译进了什么。
- Tracy / RenderDoc 默认编译期 OFF；启用走 `tools/SetupProfiling.ps1` + `-DSENTINEL_ENABLE_*=ON`；剖析构建回归用 `Validate.ps1 -EnableProfiling`。