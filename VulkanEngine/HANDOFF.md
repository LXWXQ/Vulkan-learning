# 交接说明（HANDOFF）

> 写于 2026-10-16，交接给"新开一个对话继续开发"。上一轮对话上下文用到 50% 时做的交接。
> **这份文档的用途是让新会话不用重新摸索就能接手。**读完之后请优先读 `AI_Readme.md`
> （上手/构建/坑/待办）和 `ARCHITECTURE.md`（设计取舍）——它们是项目的主文档，本文件不重复它们。

---

## 0. 一句话状态

**家庭工作区：D-4 / D-5 已提交（`cd7d695`），D-6 Job System + D-7 EnTT ECS 已完成（本地未提交）。2 帧在飞 + 异步 compute + CPU worker pool + EnTT 场景/Transform 层级/空间索引；四种渲染自检组合各跑 120 帧，stderr 全部 0 字节。**

> 当前 HEAD 是 `cd7d695`（D-4 / D-5）；D-6 / D-7 改动在工作区，尚未 commit。
>
> **本次会话追加（2026-10-17）**：TD-01 ~ TD-09 全部完成并通过
> `tools/Validate.ps1` 回归；技术债清单已清空，后续回到 `FUTURE_PLAN.md` 的主功能路线。

## 0.1 新对话入口（优先读这里）

从新对话接手时，按顺序读：

1. **`TECH_DEBT.md`** —— 当前真正要解决的问题与优先级，本文档现在是技术债主清单。
2. `FUTURE_PLAN.md` —— 地形 / 体素 / GPU-driven rendering 等未来主功能，不要和当前技术债混在一起。
3. 本文件 —— 当前基线、已完成里程碑和历史交接信息。
4. `AI_Readme.md` / `ARCHITECTURE.md` —— 深入手册与设计文档。

下一步工作不是继续加主功能，而是从 `TECH_DEBT.md` 的 **TD-01 -> TD-02** 开始。

```
9cee23b  技术债：登记 D-3 自己引入的两条
9584f41  文档：补上 D-3
bc703c0  D-3: Bindless 材质描述符（descriptorIndexing）
9f1686b  技术债清单：登记 W 阶段自己引入/暴露的 4 条
c0535f6  文档：补上 W1 / W2 两个阶段
4986022  W2: 渲染图语义校验
25ae42f  W1: 运行时配置持久化
aabfffa  文档：更新到当前架构
3fed268  修掉「关闭 SSAO 会让环境光乘未定义内容」+ Pass 开关纳入自动回归
3bb7de9  D-2 补充：Pass 开关命令行自检
adb622d  D-2: PresentPass + HDR 中间目标 + Bloom
4c75889  D-1(b) / 0cf04ee D-1(a) / 477908f C / 7146077 B / 64dbb62 B-prep / 8a6406a A / ade374c baseline
```

`git status` 必须是干净的。**如果新会话发现工作区有未提交改动，先问清楚再动。**

---

## 1. 交接时实测的基线（数字都是真的，不是抄的）

刚跑过一遍验证（命令见第 3 节）：

| 项 | 值 |
|---|---|
| 验证层 stderr | **0 字节** |
| `[Perf]` | **500 FPS** |
| draw calls / triangles / vertices | 104 / 270459 / 811377 |
| **desc binds** | **1** ← D-3 的成果（改前是 104） |
| 截屏均值 RGB | (173.6, 176.1, 176.9) |
| 暗部 / 过曝 | 19.4% / 7.5% |

> 注：均值和 D-2 记录的 (182.3, 184.5, 184.6) 差得多，**不是回归** —— 原因是窗口位置每次不同，
> 截图包含的 ImGui 面板占比不同。所以**均值只能和"同一次会话、同一窗口位置"的图比**。
> 真正可靠的判据是第 3 节的"排除 UI 面板后逐像素对比"。

---

## 2. 这一轮（W1/W2/D-3）做了什么，为什么

上一轮从"D-2 完成"接手，用户确认的顺序是：**先 W1、再 W2、然后 D-3**。
（原本计划直接上 D-3，是我建议插一个 W 阶段的 —— 因为 D-2 调参扫了四组值、每次都要重编译。）

| 阶段 | 解决什么 | 关键产出 | 提交 |
|---|---|---|---|
| **W1** | "改参数必须重编译" | `EngineConfig`（json 读写）+ 面板 Save/Reload + 退出自动保存。**实测只改 json 不重编译，exposure 1.0→2.0，过曝像素 7.5%→34%** | `25ae42f` |
| **W2** | "同步正确 ≠ 语义正确" | `RenderGraph::validateWriteCoverage()`：瞬时资源每帧必须有人写，否则 `endFrame` 直接抛异常并指名道姓 | `4986022` |
| **D-3** | 104 次材质绑定 | 材质改成 4 个纹理数组 + `materialId` 走 push constant。**`desc binds` 104 → 1，FPS 452 → 498** | `bc703c0` |

### W1 顺带挖出一个一直存在的真 bug（值得单独知道）

`renderPipeline` 是值成员、在 `Application.h` 里声明得比 `vulkanDevice` **晚** → 成员逆序析构时
它排在 `vulkanDevice` **之后** → Pass 持有的 VMA 分配在 `vmaDestroyAllocator` 之后才释放 → VMA 断言崩溃。

**它为什么藏了这么久**：验证脚本一直用 `TerminateProcess` 杀进程，而它**根本不跑析构函数**。
也就是说——**在那之前，"验证层 0 字节"这个结论从来没覆盖过退出路径**。

现在必须用 `WM_CLOSE` 让主循环自己退出。详见 `AI_Readme.md` 坑 15。

---

## 3. 怎么验证（这是这个项目最重要的工作习惯）

> **TD-01 起优先执行仓库内的 `tools/Validate.ps1`**：它会准备依赖、全新 configure/build，
> 并跑完 jobtest、四种 selftest、TD-02 跨队列回归和 TD-03 窗口缩放回归；
> 任何一步 stderr 非空都会失败。
> 下面 `%TEMP%\sentinel\` 的描述保留作历史排查参考，不再是主验证入口。

### 3.1 构建

```powershell
cd 'C:\Users\Admin\Desktop\Vs code Demo\VulkanEngine'
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build -G "Visual Studio 17 2022" -A x64
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug
```

- **新增 `src/*.cpp` 后必须先 configure**（`SOURCES` 的 glob 没有 `CONFIGURE_DEPENDS`）——最高频的坑。
- 构建必须**零警告**。`git` 会在提交时警告 `LF will be replaced by CRLF`，那**不是**问题
  （Windows checkout 的正常提示，可以忽略）。

### 3.2 自动化脚本在 `%TEMP%\sentinel\`（**不在版本库里 —— 这是已登记的技术债**）

| 脚本 | 用途 |
|---|---|
| `shot.py` | 跑起来 → 抢前台 → 截屏 → **WM_CLOSE 正常关窗** → 报验证层字节数 + `[Perf]` |
| `runw2.ps1` | 语义校验回归：4 种 Pass 开关组合 + 1 个反面用例 |
| `d2stat.py` | 截屏直方图统计 |
| `d2diff_noui.py` | 逐像素对比（可排除 ImGui 面板） |
| `diff_right.py` | 只在"绝无可能是面板"的右侧区域比较（换面板尺寸时更稳） |
| `heat.py` | 分块差值热图，定位差异出现在画面哪里 |
| `setbloom.py` | 只改 `EngineSettings` 里的 bloom 三个值（用 .NET UTF8 写，避免 PowerShell 重编码） |

用法：
```powershell
$py = 'C:\Users\Admin\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
& $py "$env:TEMP\sentinel\shot.py" "$env:TEMP\sentinel\x.png" 9
& "$env:TEMP\sentinel\runw2.ps1"
```

⚠️ **这些脚本在 `%TEMP%` 下，可能被清理。**如果新会话发现它们没了：
- 最少要重写 `shot.py`（没有它就无法可靠地验证"验证层 0 字节"）。
- 关键点：① 相机是鼠标驱动的，**先把光标移出窗口**；② **必须用 `WM_CLOSE` 关窗**，
  不能 `terminate()`；③ 截图前**必须确认窗口真的到了前台**（`GetForegroundWindow() == hwnd`），
  否则截到桌面 —— 数字看着正常但毫无意义；Windows 有前台锁定，需要 `AttachThreadInput` 绕过。

### 3.3 三条"结果可信度"规则（都踩过）

1. **同构建连跑两次，排除 UI 面板后差异应该是 0.000。** 先拿到噪声下限，再去比不同构建。
   （D-3 收尾时差点把"旧截图文件"当成 0.83% 的像素回归。）
2. **对比必须排除 ImGui 面板区域**，因为面板尺寸会随代码变化。固定矩形排除会失效，
   所以现在优先用 `diff_right.py`（只比右侧 2/3）。
3. **均值漂移别当真**：受窗口位置影响。

---

## 4. D-4：多帧并行 + timeline semaphore（已完成，`cd7d695`）

### 4.1 实现结果

- `kFramesInFlight = 2`，定义在 `src/Core/FrameInFlight.h`。
- `Renderer` 改成：
  - 每帧槽位一条独立命令缓冲；
  - 一个 timeline semaphore 追踪 frame slot 的 GPU 完成值；
  - `imageAvailableSemaphores[kFramesInFlight]` + `renderFinishedSemaphores[swapchainImageCount]`；
  - 不再使用每帧 `vkWaitForFences` 串行等待。
- `FrameInfo` 拆成三个明确字段：
  - `frameInFlightIndex`：CPU/GPU 资源槽位；
  - `swapchainImageIndex`：本次 acquire 的交换链图像；
  - `frameNumber`：从启动开始的逻辑帧号。
- 所有会被 GPU 跨帧读取的瞬时资源都按 frame slot 各一份：
  - 全局 UBO；
  - G-Buffer（5 张附件 × 2）；
  - SSAO UBO + SSAO 输出；
  - HDR scene；
  - Bloom 金字塔（4 级 × 2）；
  - 对应的全局 / SSAO / Bloom / Present 描述符集。
- `DescriptorManager::releaseMaterialSet()` 增加 `vkDeviceWaitIdle` 保险，修掉技术债 22。
- `timelineSemaphore` 设备特性在 `Device::createLogicalDevice()` 中显式查询并启用。

### 4.2 验证结果

- 全新 configure + Debug build：通过，无编译警告。
- `--selftest 120`：
  - 默认；
  - `--no-bloom`；
  - `--no-ssao`；
  - `--no-bloom --no-ssao`。
- 四种组合 **stderr 全部 0 字节**。
- 正常窗口运行实测：`[Perf] 566 FPS | draw calls 104 | desc binds 1 | triangles 270459 | vertices 811377`。
- 正常关窗退出路径已覆盖，配置正常保存。

### 4.3 D-5：Compute + 异步计算队列（已完成，`cd7d695`）

- `Device` 增加 compute queue family 选择：优先独立 compute family，没有则回退 graphics family；
  当前机器是 `graphics=0 present=2 compute=2`，属于独立异步计算队列。
- 新增 `ComputePipeline`，只包含 compute stage，不经过动态渲染/顶点输入。
- `Renderer` 增加每 frame slot 一条 compute command buffer、独立 compute command pool、
  graphics/compute 双 timeline semaphore；复用 slot 前同时等待两条 timeline。
- `RenderGraph` 新增 storage buffer 基础设施：`BufferHandle` / `createBuffer` / `importBuffer` /
  `readBuffer` / `writeBuffer` / `VkBufferMemoryBarrier2`，并把 buffer 纳入语义校验。
- compute pass 现在真正记录到 compute command buffer，`isCompute` 不再是死标志。
- 新增 `ComputeProbePass` + `shaders/compute_probe.comp`：GPU 生成 1024 个 uint，
  第二 dispatch 做 checksum 到 host-visible buffer，下一轮 CPU 读回验证。
- 反面用例 `SENTINEL_SIMULATE_COMPUTE_SKIP=1` 跳过写入 pass，buffer 语义校验会明确报错。
- 当前限制：compute / graphics 之间的 producer-consumer 资源依赖尚未实现跨队列
  ownership + semaphore 调度；RenderGraph 检测到这种访问会抛异常，不会静默竞争。

验证：
- 四种 selftest 组合各 120 帧，stderr 0 字节；每轮都出现
  `[D5] async compute probe verified: frame ... checksum ...`。
- `SENTINEL_SIMULATE_COMPUTE_SKIP=1` 能被 buffer 语义校验拦下。
- 正常窗口运行实测 526 FPS（compute probe 固定开销约 36 FPS）。

### 4.4 D-6：作业系统（已完成，本地未提交）

- 新增 `src/Core/JobSystem.h/.cpp`：
  - 全局任务队列 + `std::thread` worker pool（默认 `hardware_concurrency - 1`）；
  - `submit()` 返回可等待的 `JobHandle`；
  - `parallelFor(begin,end,grain,fn)` 按 chunk 并行；
  - worker 内嵌套 `parallelFor` 顺序退化，避免线程互相等待死锁；
  - `JobHandle::wait()` 会把任务异常重新抛回调用线程。
- `FirstApp` 启动时创建 JobSystem；`--selftest` 会先执行 JobSystem 自检。
- 新增独立命令：`VulkanEngine.exe --jobtest`，不创建 Vulkan 窗口，只验证线程池。
- 自检覆盖：
  - 512 个独立 submit；
  - `parallelFor(100000)` 结果逐元素校验；
  - worker 内嵌套 parallelFor；
  - 异常传播。

验证：
- `--jobtest`：selfTest PASS，stderr 0 字节。
- 四种渲染 selftest 组合各 120 帧：stderr 0 字节，JobSystem / compute probe 自检均通过。
- 正常窗口运行实测 541 FPS。

> D-6 当前是基础设施：JobSystem 已可用，但地形 / 体素任务尚未接入。
> 全局队列和 worker 内顺序退化足够先保证正确性；chunk 数量真正变大后再按 profile
> 决定是否升级 work stealing / 任务依赖图。

### 4.5 D-7：EnTT ECS + Transform 层级 + 空间索引（已完成，本地未提交）

- 3rdParty/entt 切到 **v3.16.0**（C++17），CMake include root 改为 `3rdParty/entt/src`。
- 删除手写 `GameObject`，场景改为 `entt::registry`：
  - `TransformComponent` / `WorldTransformComponent`
  - `HierarchyComponent`（parent + children）
  - `MeshComponent` / `MaterialComponent`
  - `RenderTagComponent`（isSkybox / isGrid）
  - `BoundsComponent` / `WorldBoundsComponent`
  - `CameraComponent` tag
- Transform 层级：`Scene::setParent()` 维护 children、拒绝自环和循环；每帧按
  `T * Ry * Rx * Rz * S` 计算 world matrix 和 normal matrix。
- 空间索引：新增 `SpatialIndex` 均匀网格，世界 AABB 插入 -> broad-phase AABB 查询
  -> 精确 frustum-AABB 测试；`GeometrySystem` 只遍历 `Scene::visibleEntities`。
- 渲染接入：GeometrySystem 从 EnTT view 读取 mesh / world transform / material tag；
  场景 draw calls 仍是 104，说明 Sponza + skybox 没被误剔除。
- `Scene::selfTest()` 覆盖父子/孙节点世界变换传播和空间网格查询。

验证：
- `--jobtest` PASS，stderr 0 字节。
- 四种渲染 selftest 组合各 120 帧，stderr 0 字节；
  `[Scene] ECS/spatial selfTest PASS` 每轮都出现。
- 正常窗口实测约 502 FPS，update CPU 约 125 us。
- `[Perf] draw calls 104 | desc binds 1 | triangles 270459 | vertices 811377` 与 D-6 一致。
- PrintWindow 截图确认画面与 ImGui 正常。

> 当前限制：glTF 内部节点层级仍然在导入时烘焙到世界空间；D-7 提供的是
> entity 级别的动态 Transform 层级。要把 Sponza 内部节点做成可动对象，
> 还需要保留 glTF 节点/子网格实例层级。

### 4.6 下一步

当前不再按 D 里程碑继续推进，转入专门的技术债阶段。优先级与验收标准见
`TECH_DEBT.md`：

1. ~~**TD-01**：依赖与验证脚本可复现。~~ 已完成：新增 `tools/SetupDependencies.ps1`、
   `tools/Validate.ps1`、`tools/README.md`，一条命令完成依赖 + 全新构建 + 全部自检。
2. ~~**TD-02**：compute/graphics 跨队列 producer/consumer 同步。~~ 已完成：
   `crossQueue` CONCURRENT 资源、RenderGraph 单向依赖推导/环检测、Renderer timeline
   submit 顺序，以及 `--cross-queue-test` 双方向 GPU 验收探针。
3. ~~**TD-03**：窗口缩放 / swapchain 重建。~~ 已完成：可缩放窗口、帧边界
   swapchain/ImGui/semaphore 重建、RenderGraph 原位 resize、动态分辨率接口与
   `--resize-test` 回归。
4. ~~**TD-04**：材质槽位归还的 `vkDeviceWaitIdle`。~~ 已完成：graphics timeline
   延迟回收队列 + `--material-stress` 150 轮回归。
5. ~~**TD-05**：RenderGraph 子资源语义校验。~~ 已完成：mip/layer 独立状态、
   subresource barrier、buffer 区间覆盖校验与正反例回归。
6. ~~**TD-06**：清理 Bloom 关闭时的固定 pass 开销。~~ 已完成：关闭时跳过
   7 个 Bloom Pass，Present 使用独立单输入管线。
7. ~~**TD-07**：ComputeProbePass 固定开销。~~ 已完成：默认关闭，`--compute-test`
   保留独立 GPU 写 + CPU 读回回归。
8. ~~**TD-08**：遗留代码 / 废弃接口。~~ 已完成：删除 Descriptor 旧系统、废弃
   shader/源目录 `.spv`、Swapchain depth/Framebuffer、IBLBaker 残留和 Renderer `#if 0`。
9. ~~**TD-09**：配置版本迁移。~~ 已完成：v0 -> v1 -> v2 显式迁移、备份、
   损坏/未来版本保护、`--config-test` 回归。技术债清单清空。

主功能路线（地形 / 体素 / GPU-driven rendering / 资产管线）见 `FUTURE_PLAN.md`。

## 5. 已登记的技术债（25 条，别重复发现）

完整清单在 `AI_Readme.md` 第 12 节 + `ARCHITECTURE.md` 第 11 节。**W / D-3 / D-5 / D-6 / D-7 引入或暴露的**是 17~25：

| # | 内容 | 何时处理 |
|---|---|---|
| 17 | **"关闭 Bloom"仍有 7 个全屏 Pass 的固定开销** —— W2 为了让语义校验的不变量成立，把 Bloom 关闭从"跳过 Pass"改成"强度置零"。视觉等价，但 Pass 照跑，所以关掉 Bloom 并不省 GPU | Bloom 变重时改两套描述符绑定 |
| 18 | ~~`EngineConfig` 没有版本迁移~~ → **TD-09 已修**：显式 v0->v1->v2 迁移 + 备份 + 损坏保护；多套预设仍未实现 | 有需求时 |
| 19 | ~~W2 的语义校验只管"图"，覆盖不到 buffer 和单个 mip/层~~ → **D-5 已扩展到 storage buffer**；单个 mip/层仍待有需要时补 | ✅ 已完成主体 |
| 20 | 验证脚本的两个前提（正常关窗 / 窗口在前台）没有自动化保护，脚本在 `%TEMP%` 下不在版本库 | 建议尽早把脚本迁进仓库 |
| 21 | bindless 的 `normalMaps` 数组绑了但没被采样（切线空间法线贴图仍有意关闭），256 材质白占 256 个描述符 | 启用切线法线贴图时 |
| 22 | ~~运行期复用材质槽位会改写 GPU 可能正在读的数组项~~ → **D-4 已修**：`releaseMaterialSet()` 在归还前执行 `vkDeviceWaitIdle`。热路径要优化时再换 timeline 延迟回收。 | ✅ 已完成 |
| 23 | **D-5 的异步 compute 尚不支持跨队列 producer/consumer**：compute 写、graphics 读（或反向）目前会被 RenderGraph 显式拒绝；GPU culling / 体素 indirect buffer 接入前必须补 ownership + semaphore 调度 | 接入 GPU 剔除/体素生成前 |
| 24 | **D-6 JobSystem 是正确性优先的基础版**：全局队列、无 work stealing、无任务依赖图、无取消；worker 内 nested parallelFor 顺序退化 | 地形/体素接入后按 profile 升级 |
| 25 | **D-7 的 glTF 内部节点层级仍未保留**：导入时几何已烘焙到世界空间，entity 级 Transform 只能整体变换；且空间网格目前每帧重建、没有 dirty/增量更新 | 需要场景内局部动态物体 / 大规模地形 chunk 时 |

> **17 是我明确不满意的那个交易**，不是"顺手记一下"。第 4 节开头提到的"D-4 顺带修 22"也是我主动提出的。

---

## 6. 新会话的开场建议

1. `git log --oneline` + `git status` 确认起点（应该是干净、HEAD = `9cee23b`）。
2. 读 `AI_Readme.md`（尤其第 3 节构建、第 10 节坑、第 11 节验证工作流、第 12 节待办）。
3. 确认 `%TEMP%\sentinel\shot.py` 还在；不在就先重建（第 3.2 节列了关键点）。
4. 跑一遍基线验证，确认 500 FPS / desc binds 1 / 0 字节对得上。
5. 然后开 D-4，**并把技术债 22 一起修掉**。

### 给新会话的提醒

- 这个工程的注释用中文、解释「为什么」而不是「做了什么」—— 请保持。
- 用 `edit`/`write` 工具改文件，**别用 PowerShell 的 `Set-Content`/`Out-File`**
  （会按 ANSI 重编码，把中文写坏 —— 踩过一次，`FrameInfo.h` 被写坏、编译报了一屏错）。
- 提交信息请写清「为什么」，要有实测数字。
- 遇到"看起来是回归"的现象，**先确认测量本身可信**（第 3.3 节三条规则）。
