# 交接说明（HANDOFF）

> 写于 2026-10-16，交接给"新开一个对话继续开发"。上一轮对话上下文用到 50% 时做的交接。
> **这份文档的用途是让新会话不用重新摸索就能接手。**读完之后请优先读 `AI_Readme.md`
> （上手/构建/坑/待办）和 `ARCHITECTURE.md`（设计取舍）——它们是项目的主文档，本文件不重复它们。

---

## 0. 一句话状态

**仓库干净、18 个提交、构建零警告、验证层 0 字节。下一个任务是 D-4（多帧并行 + timeline semaphore）。**

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

## 4. 下一步：D-4（多帧并行 + timeline semaphore）

### 4.1 现状（问题在哪）

`Renderer` 是**单缓冲**：1 个命令缓冲、1 个 fence、2 个 semaphore。
`Renderer::beginFrame` 里 `vkWaitForFences` 等上一帧完成才开始录制 → **CPU 和 GPU 完全不重叠**。

连带的耦合（这一步必须一起改，否则会出数据竞争）：

| 东西 | 现状 | 为什么必须改 |
|---|---|---|
| 全局 UBO | **只有一份**，每帧 `memcpy` 覆盖 | 多帧在飞时，帧 N+1 会改写帧 N 正在读的数据 |
| 命令缓冲 | 1 个 | 每帧在飞需要独立的命令缓冲 |
| fence / semaphore | 1 组 | 同上 |
| `FrameInfo::frameIndex` | 装的是**交换链图像索引**，不是帧计数器 | 名字有误导性；D-4 引入 `frameInFlightIndex` 时两者语义必须分开 |
| 描述符集 | init 时写一次 | bindless 数组项若在运行期被改写会和读取重叠（见 4.3） |

### 4.2 我建议的做法（不一定是最优，但理由要讲清）

1. `Renderer` 改成 `kFramesInFlight = 2`（或 3）：per-frame 命令缓冲 + fence + timeline semaphore。
2. **UBO 改成 per-frame-in-flight 数组**（`GlobalUbo ubos[kFramesInFlight]`，或一个大 buffer + 偏移）。
   描述符集也要 per-frame，或者用动态 UBO 偏移（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`）。
3. `FrameInfo` 明确区分 `frameInFlightIndex` 与 `swapchainImageIndex`，把现在那个有歧义的
   `frameIndex` 拆成两个名字。
4. **顺带修掉 D-3 留下的那条债**（见 4.3）——timeline semaphore 正好给了"等某一帧完成"的手段。

### 4.3 ⚠️ D-4 会主动制造的风险（我专门提醒的）

D-3 让材质描述符变成 bindless 数组，而 `DescriptorManager::releaseMaterialSet` 的注释里写着：

> bindless 之后"回收槽位再复用"意味着**改写 GPU 可能正在读的数组项**。
> 当前只在初始化阶段分配（设备此时空闲）所以安全。

**多帧并行 + bindless 叠加之后，这条从"理论问题"变成"随时会踩"** —— 描述符更新和
2~3 帧在飞的读取会真正重叠。

所以 D-4 应该**顺带把这条债修掉**（加帧 fence，或上 `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` + 双缓冲），
而不是留到以后。已登记在 `AI_Readme.md` 第 12 节第 22 条。

### 4.4 D-4 之后的路线（来自 `AI_Readme.md`）

```
D-5  Compute + 异步计算队列        （⚠️ 必须同时把 W2 的语义校验扩展到 buffer —— 现在只管"图"）
D-6  作业系统（线程池）
D-7  EnTT ECS + Transform 层级 + 空间索引
```

---

## 5. 已登记的技术债（22 条，别重复发现）

完整清单在 `AI_Readme.md` 第 12 节 + `ARCHITECTURE.md` 第 11 节。**这一轮自己引入/暴露的**是 17~22：

| # | 内容 | 何时处理 |
|---|---|---|
| 17 | **"关闭 Bloom"仍有 7 个全屏 Pass 的固定开销** —— W2 为了让语义校验的不变量成立，把 Bloom 关闭从"跳过 Pass"改成"强度置零"。视觉等价，但 Pass 照跑，所以关掉 Bloom 并不省 GPU | Bloom 变重时改两套描述符绑定 |
| 18 | `EngineConfig` 没有版本迁移、没有多套预设（`version` 字段写了但没人读） | 有需求时 |
| 19 | **W2 的语义校验只管"图"，覆盖不到 buffer 和单个 mip/层** | **D-5 必须一并扩展** |
| 20 | 验证脚本的两个前提（正常关窗 / 窗口在前台）没有自动化保护，脚本在 `%TEMP%` 下不在版本库 | 建议尽早把脚本迁进仓库 |
| 21 | bindless 的 `normalMaps` 数组绑了但没被采样（切线空间法线贴图仍有意关闭），256 材质白占 256 个描述符 | 启用切线法线贴图时 |
| 22 | **运行期复用材质槽位会改写 GPU 可能正在读的数组项，且无防护**（只有注释） | **D-4 顺带修** |

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
