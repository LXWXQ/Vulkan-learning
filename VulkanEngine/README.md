# Sentinel Vulkan Engine

一个面向学习与工程验证的 Vulkan 1.3 渲染器。项目重点不是堆功能，而是把**渲染同步、资源生命周期、自动化回归**做成可验证的工程基础。

当前已完成：

- 线性 RenderGraph：自动 image/buffer barrier、动态渲染、子资源语义校验
- Bindless 材质：4 个纹理数组 + push constant 材质索引
- 2 帧在飞 + graphics/compute 双队列 + timeline semaphore
- compute/graphics 跨队列 producer-consumer 单向依赖调度
- HDR + Bloom + ACES tonemap + present 后效链
- IBL：irradiance / prefiltered / BRDF LUT 一次性 GPU 烘焙
- EnTT ECS + Transform 层级 + 均匀网格空间索引
- JobSystem CPU worker pool
- 窗口缩放、swapchain 重建、动态分辨率
- 版本化配置（v0 -> v1 -> v2 迁移、备份、损坏配置保护）
- 一条命令完成依赖准备、构建与全部回归

> 技术债 TD-01 ~ TD-09 已全部完成。深度手册见 [AI_Readme.md](AI_Readme.md)，设计取舍见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10/11 x64 |
| GPU | 支持 Vulkan 1.3 |
| GPU 特性 | dynamicRendering、synchronization2、timelineSemaphore、descriptorIndexing、fragmentStoresAndAtomics |
| Vulkan SDK | 1.3+，包含 `glslangValidator` |
| CMake | 3.15+，推荐 3.21+ |
| 编译器 | Visual Studio 2022 |
| PowerShell | Windows PowerShell 5.1+ |

Vulkan SDK 默认通过 `find_package(Vulkan REQUIRED)` 查找。找不到时设置：

```powershell
$env:VULKAN_SDK = "D:\work\vulkan"
```

## 快速开始

从仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1
```

脚本会：

1. 检查/下载固定版本的第三方依赖；
2. 检查 TD-08 dead-file guard；
3. 删除旧 `build/` 并执行全新 configure + Debug build；
4. 运行 jobtest、配置迁移、渲染自检和各项技术债回归；
5. 检查每个测试的退出码、stderr 字节数和关键 stdout 标记。

日志位于：

```text
build/validation/<timestamp>/
```

### 手动构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/SetupDependencies.ps1

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

.\build\Debug\VulkanEngine.exe
```

### 依赖与资源说明

- `3rdParty/` 和 `Resources/` 不入库。
- `tools/SetupDependencies.ps1` 可以下载固定版本的第三方依赖。
- `Resources/` 中的模型和贴图需要单独准备；本项目当前使用 Sponza、DamagedHelmet 和 HDR 环境贴图。
- 第三方依赖版本与缓存说明见 [tools/README.md](tools/README.md)。

## 命令行入口

| 参数 | 用途 |
|---|---|
| 无参数 | 正常打开窗口运行 |
| `--selftest N` | 无交互跑 N 帧后退出 |
| `--no-bloom` | 关闭 Bloom（自检与普通运行均可） |
| `--no-ssao` | 关闭 SSAO（自检与普通运行均可） |
| `--render-scale F` | 中间渲染目标倍率，范围 0.25~1.0 |
| `--resize-test` | 自检中连续缩放窗口、切换动态分辨率、最小化/恢复 |
| `--jobtest` | 只测试 CPU JobSystem，不创建 Vulkan 窗口 |
| `--compute-test` | 启用 D-5 compute probe 的 GPU 写 + CPU 读回回归 |
| `--subresource-test` | 启用 TD-05 mip/layer + buffer 区间正向探针 |
| `--subresource-error-test` | 期望 RenderGraph 拒绝写 mip0 后读 mip1 |
| `--material-stress` | 自检中反复分配/延迟归还材质槽位 |
| `--config-test` | 配置版本迁移、备份与损坏文件保护测试，不需要创建窗口 |

示例：

```powershell
# 正常窗口
.\build\Debug\VulkanEngine.exe

# 跑 120 帧自检
.\build\Debug\VulkanEngine.exe --selftest 120

# 关闭 Bloom + 动态分辨率 0.5
.\build\Debug\VulkanEngine.exe --selftest 120 --no-bloom --render-scale 0.5

# 独立 compute 回归
.\build\Debug\VulkanEngine.exe --selftest 20 --compute-test

# 配置迁移回归
.\build\Debug\VulkanEngine.exe --config-test
```

## 目录结构

```text
.
├── CMakeLists.txt
├── main.cpp
├── config/
│   └── engine.json              # 版本化运行时配置
├── shaders/                     # GLSL 源码
├── src/
│   ├── Core/                    # Application / Config / JobSystem / ImGui / 描述符管理
│   ├── Render/                  # RenderGraph 各 Pass、IBL、后效链
│   ├── RHI/                     # Device / Renderer / Swapchain / Pipeline / Texture
│   └── Scene/                   # ECS、相机、glTF/OBJ、空间索引
├── tools/
│   ├── SetupDependencies.ps1
│   ├── Validate.ps1
│   └── README.md
├── AI_Readme.md                 # 深度开发手册
├── ARCHITECTURE.md              # 架构与设计取舍
├── HANDOFF.md                   # 交接状态
├── TECH_DEBT.md                 # 技术债清单（已清空）
└── FUTURE_PLAN.md               # 后续主功能路线
```

## 已实现系统

### RenderGraph

- 线性 pass 顺序，不做重排或自动剔除。
- 根据资源访问声明自动插入 `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2`。
- 瞬时资源每帧写覆盖校验。
- mip/layer 级 `SubresourceRange` 状态跟踪。
- buffer offset/size 区间覆盖校验。
- graphics / compute 跨队列单向依赖推导，双向依赖明确报错。

### 多帧与异步 compute

- 2 个 frame-in-flight 槽位。
- graphics / compute 各自指令缓冲和 timeline semaphore。
- 需要跨队列依赖时根据方向决定 submit 顺序。
- compute probe 默认关闭，`--compute-test` 可独立验证 GPU 写 + CPU 读回。

### 材质

- 单个 bindless 材质描述符 set。
- 4 个纹理数组：albedo / normal / metallic / roughness。
- 材质切换只改 push constant。
- descriptor update-after-bind + graphics timeline 延迟槽位回收。

### 后效链

- G-Buffer -> SSAO -> Lighting HDR -> Bloom -> Present。
- Bloom 开启时 1 个亮部提取 + 3 级 downsample + 3 级 upsample。
- Bloom 关闭时完全跳过 7 个 Bloom pass，Present 使用单输入专用管线。
- Present 执行 exposure -> ACES Filmic -> gamma，并叠加 ImGui。

### 场景与工具

- EnTT ECS。
- Transform/WorldTransform 层级传播。
- 空间均匀网格 broad-phase + frustum culling。
- glTF / OBJ 加载与材质槽位分配。
- CPU JobSystem worker pool。
- 版本化配置与损坏文件保护。

## 已知边界

- 跨队列资源当前走 `VK_SHARING_MODE_CONCURRENT` 简单路径；尚未实现 EXCLUSIVE queue-family ownership transfer。
- glTF 内部节点层级在导入时烘焙到世界空间，Sponza 内部节点不是独立动态 entity。
- SpatialIndex 每帧重建，尚无 dirty / 增量更新。
- 尚未实现阴影、SSR、自动曝光、动画、音频、编辑器。
- ComputeProbe / CrossQueueProbe / SubresourceProbe 是测试探针，不是生产渲染任务。

## 进一步阅读

- [PROJECT_STATE.md](PROJECT_STATE.md)：给下一个对话的现状速览、验证入口和接手注意事项。
- [AI_Readme.md](AI_Readme.md)：逐系统实现细节、常见坑、历史演进。
- [ARCHITECTURE.md](ARCHITECTURE.md)：设计决策、资源生命周期、取舍。
- [TECH_DEBT.md](TECH_DEBT.md)：当前技术债状态。
- [FUTURE_PLAN.md](FUTURE_PLAN.md)：地形、体素、GPU-driven rendering 等后续路线。
- [tools/README.md](tools/README.md)：依赖固定版本和验证脚本说明。