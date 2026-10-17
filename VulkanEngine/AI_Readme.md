# AI_Readme — VulkanEngine "Sentinel"

> 新对话请优先读 `HANDOFF.md` 和 `TECH_DEBT.md`；未来主功能路线在 `FUTURE_PLAN.md`。
> 本文档是深入手册，旧技术债清单保留作历史证据，当前优先级以 `TECH_DEBT.md` 为准。

> 面向「接手继续开发的 AI / 新开发者」。本文档只描述**代码里真实存在**的东西。
>
> 上一次全面重写：D-2（Bloom + Present + HDR 中间目标）完成之后。
> 如果你发现文档和代码不一致，以代码为准，并顺手把这里改掉。

---

## 1. 项目定位与最终目标

**Sentinel** 是一个从零手写的 Vulkan 实时渲染引擎（不是教程复刻，是从 `baseline` 开始逐步重构出来的）。

- **当前形态**：延迟着色（Deferred Shading）+ 5 附件 G-Buffer + SSAO + split-sum IBL + 线性 HDR 后效链（Bloom → ACES → gamma）。
- **目标形态**：**以高度场地形为主的开放世界游戏引擎 + 局部体素**（对标 *No Man's Sky* / *7 Days to Die* 那一类）。
- **平台**：**只考虑 Windows 桌面**。所有"为了跨平台/移动端而做的妥协"都不需要。
  （代码里还留着 `processAndroidTouchInput()` 空桩和 `#define GLFW_CLIENT_API, GLFW_NO_API` 的 GLFW 设置，前者是历史残留、后者是 Vulkan 必需。）
- **风格约定**：注释解释「**为什么**」而不是「做了什么」。这个约定是硬性的 —— 凡是"看起来可以删掉的一行"，注释里必须写清删掉会怎样。

### 已验证的性能基线（D-2，commit `adb622d`）

```
[Perf] ~460 FPS | draw calls 104 | triangles 270459 | vertices 811377
验证层 stderr: 0 字节
截屏直方图: 均值 RGB ≈ (182.3, 184.5, 184.6) / 暗部 13.7% / 过曝 7.5%
```

（后续在不同截屏时机复测会得到 ≈ (183.2, 185.4, 185.5) / 13.5% / 8.0% —— 均值本身有 ±1 的浮动，别把 0.5 的差异当成回归。）

D-2 提交时记录的前后对照（同一个机位、鼠标移出窗口保证相机不动）：

| | D-1(b)（基线） | D-2（提交时实测） |
|---|---|---|
| FPS | 547 | **457**（−16%，代价来自 7 个额外的全屏 Pass） |
| draw calls / triangles / vertices | 104 / 270459 / 811377 | **完全一致**（说明没有渲染内容回归） |
| 均值 RGB | (183.5, 186.0, 186.0) | (182.3, 184.5, 184.6) |
| 暗部占比 | 12.7% | 13.7% |
| 过曝占比 | 7.1% | 7.5% |

这两组数字是**回归基线**：任何改动之后如果验证层开始刷屏、或均值漂移超过 ~2、过曝比例变化超过 ~1 个百分点，说明改动引入了问题（见第 11 节验证工作流）。
（不同截屏时机的均值本身会有 ±1 的浮动，别把 0.5 的差异当成回归。）

> ⚠️ `vertices` 统计的其实是**索引数**（`GeometrySystem::render` 里累加的是 `subMesh.indexCount`），不是唯一的顶点数。名字是历史遗留，看数字时心里要打折。

---

## 2. 已确认的技术方向与依赖清单

### 2.1 图形 API 与设备特性

| 项 | 值 | 说明 |
|----|-----|------|
| Vulkan | **1.3** | `Device::createInstance` 里 `appInfo.apiVersion = VK_API_VERSION_1_3` |
| `dynamicRendering` | 必需 | 1.3 核心。没有 `VkRenderPass` / `VkFramebuffer`，全靠 `vkCmdBeginRendering/EndRendering` |
| `synchronization2` | 必需 | 1.3 核心。屏障统一走 `vkCmdPipelineBarrier2` + `VkImageMemoryBarrier2` |
| `samplerAnisotropy` | 启用 | 1.0 特性，纹理采样器 `maxAnisotropy = 16` |
| `timelineSemaphore` | 必需（D-4） | Vulkan 1.2 特性；`Renderer` 用它按 frame slot 追踪 GPU 完成值 |
| 设备扩展 | 只有 `VK_KHR_SWAPCHAIN_EXTENSION_NAME` | 1.3 之后不需要再手动开 dynamic rendering / sync2 扩展 |
| 队列 | graphics / present / compute | D-5 优先选独立 compute family；没有则 compute 回退 graphics family |
| 呈现模式 | `VK_PRESENT_MODE_IMMEDIATE_KHR` | 无垂直同步，跑分友好、会撕裂 |

`Device::createLogicalDevice()` 会**先查询** `VkPhysicalDeviceVulkan13Features` 的 `dynamicRendering` / `synchronization2`，不支持就直接抛异常。这是有意的：本引擎不再为老 GPU 保留回退路径。

### 2.2 第三方库（全部在 `3rdParty/`，header-only 或直接编进目标）

| 库 | 路径 | 用途 | 实际使用情况 |
|----|------|------|--------------|
| GLFW | `3rdParty/glfw` | 窗口 / 输入 / Surface | 在用 |
| GLM | `3rdParty/glm` | 数学 | 在用 |
| Dear ImGui | `3rdParty/imgui` | 调试面板 | 在用（**1.92.6**，见第 9 节坑 3） |
| stb | `3rdParty/stb` | `stb_image`（LDR + HDR 解码） | 在用 |
| tinyobjloader | `3rdParty/tinyobjloader` | OBJ 解析 | 在用（`Model.cpp`） |
| VMA | `3rdParty/VulkanMemoryAllocator` | 显存分配 | 在用（**所有** image/buffer 都走 VMA） |
| tinygltf | `3rdParty/tinygltf` | glTF 2.0 解析（Sponza） | 在用（`GLTFLoader.cpp`） |
| EnTT | `3rdParty/entt` | ECS | 在用（**v3.16.0**，C++17） |
| **spdlog** | `3rdParty/spdlog` | 日志 | **只有 include 路径，代码里一次都没用**（现状是 `std::cout` / `std::cerr`） |

> EnTT 已在 D-7 接入；spdlog 仍只配了 include 路径，日志继续用 `std::cout` / `std::cerr`。

### 2.3 计划接入（还没进 3rdParty）

| 库 | 用途 | 对应里程碑 |
|----|------|-----------|
| Jolt Physics | 物理（地形/体素碰撞） | 未排期 |
| ozz-animation | 骨骼动画 | 未排期 |
| meshoptimizer | 网格优化 / LOD / 简化 | 未排期 |
| Tracy | 帧级 profiler | 未排期 |

### 2.4 工具链

| 项 | 值 |
|----|-----|
| 语言标准 | C++17（`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`） |
| 编译器 | MSVC。全局加了 `/utf-8`（源码是 UTF-8 无 BOM，中文注释必须有这个开关） |
| CMake | ≥ 3.15 |
| Vulkan SDK | **硬编码在 `CMakeLists.txt`**：`MY_VULKAN_DIR = "D:/work/vulkan"`，头文件 `${MY_VULKAN_DIR}/Include`、库 `${MY_VULKAN_DIR}/Lib/vulkan-1.lib` |
| Shader 编译器 | **硬编码**：`D:/work/vulkan/Bin/glslangValidator.exe` |
| 窗口 | GLFW，`1920×1080` **不可缩放**（`glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE)`），尺寸来自 `FrameInfo.h` 的 `#define WIDTH/HEIGHT` |

---

## 3. 快速构建（照做即可）

### 3.1 前置条件

1. Vulkan SDK 放在 `D:/work/vulkan`。**放别处就必须改 `CMakeLists.txt` 第 8 行和第 85 行两处路径**（一处是头/库，一处是 glslangValidator）。
2. `3rdParty/` 九个目录齐全（见 2.2）。注意 `glfw3` 是通过 `find_package(glfw3 REQUIRED)` 找的，同时 include 路径又指向 `3rdParty/glfw/include`。
3. 支持 Vulkan 1.3 + dynamicRendering 的 GPU 与驱动。

### 3.2 构建命令

```powershell
cd "C:\Users\Admin\Desktop\Vs code Demo\VulkanEngine"
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```

产物：`build/Debug/VulkanEngine.exe` + `build/Debug/vulkan-1.dll`（CMake 会把 SDK 的 dll 拷过来）。

### 3.3 构建流程里发生的三件事

1. **配置阶段生成 `AssetPaths.h`**
   `cmake/AssetPaths.h.in` 用 `configure_file` 展开成 `build/generated/AssetPaths.h`，把**绝对路径**烧成宏：
   ```cpp
   #define SENTINEL_SHADER_DIR   "<build>/shaders/"
   #define SENTINEL_RESOURCE_DIR "<工程根>/Resources/"
   ```
   这样引擎不再依赖「工作目录恰好是 `build/Debug`」这个隐式约定。**移动工程目录后必须重新 configure 一次**，否则宏里还是旧路径。

2. **Shader 在 PRE_BUILD 阶段编译**
   ```cmake
   file(GLOB SHADER_FILES CONFIGURE_DEPENDS "shaders/*.vert" "shaders/*.frag" "shaders/*.comp")
   ```
   每个 `.vert/.frag/.comp` 各挂一条 `add_custom_command(TARGET ... PRE_BUILD)`，调 `glslangValidator -V <src> -o <build>/shaders/<name>.spv`。
   `.comp` 已经在 glob 里了 —— D-5 已直接复用这段构建逻辑。

3. **全局注入 tinygltf 的两个宏**
   ```cmake
   target_compile_definitions(... TINYGLTF_NO_STB_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE)
   ```
   原因：tinygltf 内部会展开一份 `stb_image` 实现，和 `Application.cpp` 里的 `#define STB_IMAGE_IMPLEMENTATION` 撞符号。这两个宏必须对**所有**包含 `tiny_gltf.h` 的翻译单元一致，所以放在 CMake 里统一注入，而不是写在某个 .cpp 顶部。

### 3.4 ⚠️ 最容易踩的构建坑：新增源文件后必须重新 configure

```cmake
file(GLOB_RECURSE SOURCES "src/*.cpp")     # <- 注意：没有 CONFIGURE_DEPENDS
```

`src/**` 的源文件列表是**在配置阶段一次性拍死**的，没有 `CONFIGURE_DEPENDS`。
所以：

- **新增** `src/**.cpp` → 直接 `cmake --build` 不会编译它，而且**不会有任何报错**，只会在链接期冒出一堆 `unresolved external symbol`。
  正确做法：
  ```powershell
  cmake -S . -B build            # 重新配置
  cmake --build build --config Debug
  ```
- **删除/重命名** `src/**.cpp` 同理。
- Shader（`shaders/*.vert|frag|comp`）**不受影响** —— 那个 glob 带了 `CONFIGURE_DEPENDS`，加/删 shader 会自动重新 glob。
  （D-2 的 `BloomPass.cpp` / `PresentPass.cpp` / `bloom_*.frag` / `present.frag` 就是同时踩了"要重新 configure"和"shader 不用"这两条。）

### 3.5 直接跑

```powershell
.\build\Debug\VulkanEngine.exe
```

- 启动时会打印资源加载进度（`[Sentinel]` / `[DescriptorManager]` / `[IBL]` / `[GLTF]` / `[Scene]` 前缀）。
- `main.cpp` 里 `setvbuf(stdout, nullptr, _IONBF, 0)` 关掉了 stdout 缓冲，所以日志是实时的 —— 排查"卡死在哪一步"时很有用。
- 窗口不可缩放、不可最大化调整尺寸；关掉窗口退出。

### 3.6 无人值守自检（D-2 新增）

```powershell
.\build\Debug\VulkanEngine.exe --selftest                    # 正常状态跑 600 帧后退出
.\build\Debug\VulkanEngine.exe --selftest 900                # 指定帧数
.\build\Debug\VulkanEngine.exe --selftest --no-bloom         # 关掉 Bloom
.\build\Debug\VulkanEngine.exe --selftest --no-ssao          # 关掉 SSAO
.\build\Debug\VulkanEngine.exe --selftest --no-bloom --no-ssao
.\build\Debug\VulkanEngine.exe --jobtest                     # D-6 JobSystem 独立自检（不创建 Vulkan 窗口）
```

走的是**完整的** `update()` + `render()`（含 ImGui 构建与提交、present），只是用固定 `dt = 1/60` 推进、不依赖窗口事件循环，跑完 N 帧后 `vkDeviceWaitIdle` 退出。
两个开关走的是和 ImGui 复选框**完全相同**的代码路径（`FirstApp::setBloomEnabled` / `setSSAOEnabled` → `EngineSettings`）。

**为什么需要它**：`ImGui` 的复选框没法在无人值守的验证流程里点，而"**某个 Pass 被关掉**"恰恰是最容易漏验证层错误的分支 —— 中间目标整帧没人写（布局一直停在 `UNDEFINED`），后面的 Pass 却仍然声明要采样它。把它做成命令行开关之后，这条分支才能进"验证层 0 字节"的自动回归。

四种组合当时实测结果（各 300 帧）：**验证层 stderr 全部 0 字节**。
（Bloom-off 900 帧已实测：stderr 0 字节。）

---

## 4. 目录结构（逐文件一行）

```
VulkanEngine/
├── CMakeLists.txt              # 依赖、源文件 glob、AssetPaths 生成、shader 编译
├── main.cpp                    # 入口：关 stdout 缓冲、chcp 65001、解析命令行（--selftest / --no-bloom / --no-ssao / --jobtest）、构造 FirstApp、try/catch
├── AI_Readme.md                # 本文档
├── ARCHITECTURE.md             # 架构设计文档
├── cmake/
│   └── AssetPaths.h.in         # 生成 AssetPaths.h 的模板（烧绝对路径）
├── 3rdParty/                   # 见 2.2
├── Resources/
│   ├── Models/
│   │   ├── DamagedHelmet/      # OBJ + PBR 贴图，Sponza 加载失败时的回退场景
│   │   ├── Sponza/glTF/        # 当前主场景（Sponza.gltf）
│   │   ├── debug_cube.obj      # 早期调试用立方体
│   │   ├── sphere.obj          # 天空盒球体（1/10 缩放 + 跟随相机）
│   │   ├── plane.obj / Quad-Sphere.obj / teapot.obj   # 备用调试模型
│   ├── Texture/
│   │   ├── environment.hdr     # 当前使用的等距柱状 HDR 环境（天空盒 + IBL 源头）
│   │   ├── environment_indoor.hdr  # 备用 HDR
│   │   ├── albedo.jpg / normal.jpg / metallic.jpg / roughness.jpg  # 散装 PBR 贴图
│   │   ├── square_green.png    # 备用
│   │   ├── RuslLessRL/ RustLessRH/ RustedMetal/   # 更多 PBR 贴图集（当前未使用）
│   │   └── Readme.md
├── shaders/                    # 见第 8 节着色器一览（含 3 个已废弃但仍在的 shader）
├── RTR4_Notes/                 # 学习笔记（chapters/Ch07_Shadows.md + images/），与引擎无关
└── src/
    ├── Core/
    │   ├── Application.h/.cpp        # FirstApp：构造顺序 orchestrator + run/update/render
    │   ├── Descriptor.h/.cpp         # 【已废弃】DescriptorAllocator + DescriptorBuilder，不在渲染路径上
    │   ├── DescriptorManager.h/.cpp  # 描述符池 / Set 0 全局 / Set 1 材质槽位（唯一在用的描述符系统）
    │   ├── FrameInFlight.h          # kFramesInFlight（当前 2）
    │   ├── JobSystem.h/.cpp         # D-6 CPU 线程池 / submit / parallelFor
    │   ├── FrameInfo.h               # GlobalUbo + EngineSettings + RenderTelemetry + FrameInfo（每帧数据总线）+ std140 静态断言
    │   ├── ImGuiSystem.h/.cpp        # ImGui 初始化 + 面板 + prepareDrawData/renderDrawData 两段式
    │   ├── Material.h                # PBR 材质 + 缺贴图时的静态兜底 VkDescriptorImageInfo
    │   ├── MaterialSlot.h            # kInvalidMaterialSlot = UINT32_MAX（**不要用 0 当哨兵**）
    │   └── MaterialSystem.h/.cpp     # 材质/纹理缓存 + 1x1 默认贴图 + buildMaterialDescriptor
    ├── Render/
    │   ├── IRenderPass.h             # Pass 抽象：init() / addToGraph() / getName()
    │   ├── RenderPipeline.h          # 有序持有 Pass，initAll() / registerAll() / findPass()
    │   ├── RenderResources.h         # 帧内资源登记表 + kHdrSceneFormat（**同步关系的唯一事实来源**）
    │   ├── GeometryPass.h/.cpp       # 导入 G-Buffer 5 张附件，声明 Geometry pass
    │   ├── GeometrySystem.h/.cpp     # 3 条几何管线 + 逐子网格材质切换 + 遥测累加
    │   ├── SSAOPass.h/.cpp           # SSAO：自建 UBO/噪声贴图/输出图/采样器 + fullscreen 管线
    │   ├── LightingPass.h/.cpp       # 导入交换链图像 + 创建 HDR 图 + 声明 Lighting pass
    │   ├── LightingSystem.h/.cpp     # 光照全屏管线（唯一一条读全局 Set 0 的片元着色器）
    │   ├── BloomPass.h/.cpp          # D-2：亮部提取 + 3 级降采样 + 3 级上采样（7 个 pass / 7 个 set）
    │   ├── PresentPass.h/.cpp        # D-2：曝光 + Bloom 叠加 + ACES + gamma → 交换链，并叠加 ImGui
    │   └── IBLBaker.h/.cpp           # 一次性 GPU 烘焙：envCube / irradiance / prefiltered / BRDF LUT
    ├── RHI/
    │   ├── CommandUtils.h            # cmdSetFullViewportAndScissor / cmdBeginSingleTime / cmdEndSingleTime
    │   ├── RenderGraph.h/.cpp        # 线性渲染图：自动屏障 + 动态渲染作用域（**核心**）
    │   ├── Device.h/.cpp             # 实例/物理设备/逻辑设备/队列/VMA + 验证层 + findDepthFormat
    │   ├── Swapchain.h/.cpp          # 交换链 + ImageView（**depth 资源已无人使用**，见 12 节）
    │   ├── Renderer.h/.cpp           # 帧生命周期：2 帧在飞 + graphics/compute 双命令流 / 双 timeline
    │   ├── GBuffer.h/.cpp            # 5 附件的创建/销毁 + GBufferFormat 格式常量 + NEAREST 采样器
    │   ├── Pipeline.h/.cpp           # 图形管线 + ComputePipeline 封装
    │   ├── Texture.h/.cpp            # 文件/内存 → 纹理；LDR(sRGB/UNORM) 与 HDR(RGBA32F) 两条路径
    │   └── CubemapTexture.h/.cpp     # 立方体贴图：一个 CUBE view + 每 (mip, face) 一个 2D view
    └── Scene/
        ├── Scene.h/.cpp              # EnTT registry + 场景装配 + world transform + frustum culling
        ├── Components.h              # Transform/Hierarchy/Mesh/Material/RenderTag/Bounds/Camera 组件
        ├── SpatialIndex.h/.cpp       # 均匀网格 broad phase + AABB 查询
        ├── Camera.h/.cpp             # VulkanCamera：手写透视矩阵 + YXZ 欧拉视图矩阵
        ├── CameraController.h/.cpp   # FPS + Orbit 双模式输入 + 阻尼平滑
        ├── Model.h/.cpp              # Vertex/SubMesh + Device-Local 顶点索引缓冲（staging 上传）+ OBJ 解析
        └── GLTFLoader.h/.cpp         # tinygltf 加载 glTF（几何烘焙到世界空间 + 逐子网格材质槽位）
```

---

## 5. 渲染管线全景

### 5.1 Pass 顺序

```
GeometryPass            写 G-Buffer 5 张附件（4 颜色 + 深度，全部 CLEAR）
   ↓
SSAOPass                读 G-Buffer.Position/Normal → 写 R8_UNORM 遮蔽图
   ↓
LightingPass            读 G-Buffer + SSAO + IBL 三件套 + 环境图 → 写 R16G16B16A16_SFLOAT（线性 HDR）
   ↓
BloomPass               Bright: HDR → level0（1/2 分辨率）
                        Down0..2: level0 → level1 → level2 → level3
                        Up2..0:  level3 → level2 → level1 → level0（加法混合累加）
   ↓
PresentPass             HDR + Bloom*intensity → 曝光 → ACES → gamma(1/2.2) → 交换链，
                        并在同一个 vkCmdBeginRendering 作用域里叠加 ImGui
   ↓
PresentTransition       RenderGraph::presentTransition()：只发一个布局转换到 PRESENT_SRC_KHR
```

渲染图内部注册出来的 pass 名字依次是：
`Geometry`, `SSAO`, `Lighting`, `Bloom.Bright`, `Bloom.Down0/1/2`, `Bloom.Up2/1/0`, `Present`, `PresentTransition`。

> `PresentTransition` 不是 `IRenderPass`，它是 `RenderGraph::presentTransition()` 临时 `addGraphicsPass` 出来的一个**没有颜色/深度附件**的 pass —— 渲染图看到没有附件就只发屏障、不 `vkCmdBeginRendering`。

### 5.2 为什么是这个顺序（而不是别的）

- **SSAO 必须在 Lighting 之前**：SSAO 调制的是环境光项（`ambient *= ssao`），是光照的输入。
- **Bloom 必须在 tonemap 之前**：ACES 的最后一步是 `clamp(..., 0, 1)`，一旦高光被压到 1.0，Bloom 只能"把白色再涂白"。所以 Bloom 作用在线性 HDR 上。
- **曝光必须在 Bloom 之后**：曝光本质上是 tonemap 的输入缩放。如果曝光写在光照通道里，Bloom 的阈值会随曝光一起变，拖曝光滑块时辉光强度会乱跳（这是 D-2 明确修掉的行为）。
- **ImGui 必须最后画**：放在 Present 里，意味着以后往 Present 之前插任何后效（SSR / 色彩分级 / 暗角），UI 都自动不会被糊掉。

### 5.3 G-Buffer 布局（5 个附件）

格式常量在 `src/RHI/GBuffer.h` 的 `namespace GBufferFormat`，**唯一来源**；`GeometrySystem` 建立管线时的 `colorAttachmentFormats` 必须和它完全一致，否则动态渲染管线与渲染目标不匹配。

| 附件 | 格式 | 内容 | 谁写 | 谁读 |
|------|------|------|------|------|
| Position | `R16G16B16A16_SFLOAT` | 世界空间位置，`.w = 1` | mrt.frag / grid_mrt.frag / skybox_mrt.frag | SSAO, Lighting |
| Normal | `R16G16B16A16_SFLOAT` | 世界空间法线，`.w = 1`；**天空盒写 `(0,0,0)` 作为"非几何"标记** | 同上 | SSAO, Lighting |
| Albedo | **`R16G16B16A16_SFLOAT`** | 线性基础色；**天空盒往这里写线性 HDR 辐照度（可以 > 1）** | 同上 | Lighting（透传） |
| Pbr | `R8G8B8A8_UNORM` | `.r = 金属度`（取 metallicRoughness 贴图的 `.b`），`.g = 粗糙度`（取 `.g`） | mrt.frag / grid_mrt.frag | Lighting |
| Depth | `findDepthFormat()` → 首选 `D32_SFLOAT` | 深度 | Geometry | （只有深度测试用） |

**为什么 Albedo 是 16F**（D-2 从 `R8G8B8A8_SRGB` 改过来的）：
天空盒把"线性 HDR 辐照度"直接写进 albedo 通道，由 Lighting 透传、由 PresentPass 统一 tonemap。8bit UNORM/SRGB 会在写入时把 > 1.0 的太阳/亮斑直接截断 —— 截断后的 1.0 恰好又超过 Bloom 阈值，结果**整块天空都在起辉光**，变成一层假雾。改成 16F 之后，> 1 的高光才能活到 Bloom。

**天空盒的"接头暗号"**：`skybox_mrt.frag` 把 `outNormal` 写成 `(0,0,0)`，`lighting.frag` 和 `ssao.frag` 都靠 `length(N) < 0.1` 判断"这是天空，原样透传 / 跳过遮蔽"。

### 5.4 中间目标

| 目标 | 格式 | 尺寸 | 谁持有 | 谁写 / 谁读 |
|------|------|------|--------|-------------|
| `ssao` | `R8_UNORM` | 1920×1080 | 渲染图（`createTexture`） | SSAOPass 写 / LightingPass 读 |
| `hdrScene` | `R16G16B16A16_SFLOAT`（`kHdrSceneFormat`） | 交换链尺寸 | 渲染图 | LightingPass 写 / BloomPass + PresentPass 读 |
| `bloomLevels[0..3]` | 同 `hdrScene` | 960×540 / 480×270 / 240×135 / 120×67 | 渲染图 | BloomPass 内部读写 |
| `bloomResult` | = `bloomLevels[0]` | 960×540 | 渲染图 | PresentPass 采样 |

---

## 6. 描述符集布局

### Set 0 —— 全局（`DescriptorManager::createGlobalLayout`，10 个 binding）

一次性构建（`Application` 构造函数里 `buildGlobalSet`），之后所有 pass 只读。

| binding | 类型 | 内容 | stage |
|---------|------|------|-------|
| 0 | `UNIFORM_BUFFER` | `GlobalUbo`（投影视图矩阵 / 方向光 / 相机位置 / 点光源数组 / `numLights` / `exposure`） | VERTEX \| FRAGMENT |
| 1 | `COMBINED_IMAGE_SAMPLER` | `environmentMap`：等距柱状 HDR（天空盒采样） | FRAGMENT |
| 2 | 同上 | `inPosition`（G-Buffer） | FRAGMENT |
| 3 | 同上 | `inNormal`（G-Buffer） | FRAGMENT |
| 4 | 同上 | `inAlbedo`（G-Buffer） | FRAGMENT |
| 5 | 同上 | `inPbr`（G-Buffer） | FRAGMENT |
| 6 | 同上 | `inSSAO` | FRAGMENT |
| 7 | 同上 | `irradianceMap`（samplerCube，漫反射 IBL） | FRAGMENT |
| 8 | 同上 | `prefilteredMap`（samplerCube，镜面 IBL，5 级 mip） | FRAGMENT |
| 9 | 同上 | `brdfLut`（split-sum 的 BRDF 积分表） | FRAGMENT |

> 采样器不是渲染图管的 —— 渲染图只管图像。`ssaoSampler` 来自 `SSAOPass`，IBL 的三个采样器来自 `IBLBaker` / `CubemapTexture`，G-Buffer 采样器来自 `GBuffer`（NEAREST + CLAMP_TO_EDGE）。

### Set 1 —— 材质（bindless，4 个纹理数组）

| binding | 内容 | 着色器用法 |
|---------|------|-----------|
| 0 | `sampler2D albedoMaps[]` | `texture(albedoMaps[nonuniformEXT(matId)], uv)` |
| 1 | `sampler2D normalMaps[]` | （当前未采样，见下） |
| 2 | `sampler2D metallicMaps[]` | `.b` 取金属度 |
| 3 | `sampler2D roughnessMaps[]` | `.g` 取粗糙度 |

**这是 D-3 的核心变化**：不再是"每个材质一个 `VkDescriptorSet`"，而是**一个 set 里放 4 个纹理数组**，材质下标通过 push constant 传进着色器。

- 容量 **256 个槽位**（`descriptorManager->init(*vulkanDevice, 256)`），长度用 `VkDescriptorSetVariableDescriptorCountAllocateInfo` 在**分配时**指定 —— 想调容量只改这个数，布局不用动。
- **整帧只绑一次 set 1**（`GeometrySystem::render` 把绑定挪到了循环外），切材质只是改 push constant 里的一个整数。实测 `desc binds` 从 104 降到 **1**。
- 槽位**可分配可回收**：`allocateMaterialSet()` / `releaseMaterialSet()`。
  ⚠️ 但 `releaseMaterialSet()` **目前没有任何调用方**；而且 bindless 之后"回收再复用"意味着**改写 GPU 可能正在读的数组项** —— 当前只在初始化阶段分配（设备此时空闲）所以安全，将来要在运行期动态分配材质，必须加帧 fence 或上 `UPDATE_AFTER_BIND` + 双缓冲。
- 未分配时的哨兵是 **`kInvalidMaterialSlot = UINT32_MAX`**（`Core/MaterialSlot.h`），**不是 0**。原因见第 9 节坑 4。
- **`materialValid` 必须走 push constant**：天空盒 / 程序化网格不采材质数组，而它们的槽位可能压根没写过内容（`descriptorBindingPartiallyBound` 允许数组尾部未绑定，但**采它就是未定义行为**）。所以着色器里先看 `push.material.y`，为 0 就直接输出顶点色 + 默认 PBR。

**为什么是 4 个独立数组而不是一个 struct 数组**：GLSL 不能把 `sampler2D` 放进结构体数组再索引，拆开最省事；四个数组共用同一个下标，读起来反而更清楚。

### D-3 的开关与三个必踩的规范要求

启用 `descriptorIndexing` 需要 Vulkan 1.2 的 **5 个次特性**（`Device::createLogicalDevice`）。实测被验证层当场拦下三个错，都是"读文档很容易漏"的硬性要求：

| # | 要求 | 漏了会怎样 |
|---|------|-----------|
| 1 | `descriptorBindingVariableDescriptorCount` 必须开 | 它是**独立于 `runtimeDescriptorArray` 的开关**，少了它布局直接被拒 |
| 2 | 同一 set 里 `VARIABLE_DESCRIPTOR_COUNT_BIT` **只能加在编号最大的 binding** 上 | `VUID-...-pBindingFlags-03004` |
| 3 | 分配时的长度必须 **≤ 布局里声明的 `descriptorCount`** | `VUID-...-pSetLayouts-03046`。布局里的 `descriptorCount` 是**上限**，不是最终长度 |

还有一个易错点：**用 `VkPhysicalDeviceFeatures2` 链传特性时，`VkDeviceCreateInfo::pEnabledFeatures` 必须为 `nullptr`**（`VUID-VkDeviceCreateInfo-pNext-04748`）。特性链是 `1.3 -> 1.2`。

### 其他 set（局部，不走 DescriptorManager 的两个主 layout）

| 使用者 | layout 从哪来 | 内容 | 数量 |
|--------|---------------|------|------|
| `SSAOPass` | `descriptorManager->createSetLayout()` | binding 0 = `SSAOUbo`，binding 1 = 4×4 噪声贴图 | 1 |
| `BloomPass` | 自己 `vkCreateDescriptorSetLayout` | binding 0 = 一张 sampler2D | 7（1 bright + 3 down + 3 up） |
| `PresentPass` | 自己 `vkCreateDescriptorSetLayout` | binding 0 = `hdrScene`，binding 1 = `bloomResult` | 1 |

`DescriptorManager::init` 里的池容量就是按这个账算的：`maxSets = maxMaterials + 64`，`COMBINED_IMAGE_SAMPLER = maxMaterials*4 + 96`。
⚠️ 这两个魔法数（+64 / +96）**和 Bloom 的级数耦合**：如果把 `BloomPass::kLevelCount` 从 4 调大很多（每级 +2 个 set），要回来同步加余量。

---

## 7. 渲染图：用法与设计取舍

### 7.1 它想解决什么

在这之前，每个 Pass 都要手写 `vkCmdPipelineBarrier`（旧布局 → 新布局、src/dst stage、src/dst access）。这类代码"写对了没奖励、写错了只在验证层里出现、而且很难看出是哪一句漏了"。

渲染图把这件事变成**声明**：

```cpp
auto& p = graph.addGraphicsPass("Bloom.Bright");
p.read(resources.hdrScene);                                       // 我要当纹理采样
p.writeColor(resources.bloomLevels[0], VK_ATTACHMENT_LOAD_OP_CLEAR, clear);  // 我要当颜色附件写
p.execute([&](VkCommandBuffer cmd) { /* 只写绘制，不写同步 */ });
```

屏障、布局转换、`vkCmdBeginRendering/EndRendering`、`viewport/scissor` 全部由 `endFrame()` 负责。**Pass 里不会再出现一行同步代码。**

### 7.2 API 速查

```cpp
// 帧生命周期（Application::render 里这样用）
graph.beginFrame(commandBuffer);
renderPipeline.registerAll(graph, frameInfo);   // 各 Pass 往里声明
graph.presentTransition(resources.swapchainImages[frameInfo.frameIndex]);
graph.endFrame();                               // 这里才真正推导屏障并录制

// 资源
RenderGraph::TextureHandle createTexture(const TextureDesc&);          // 图创建并持有（Bloom/SSAO/HDR 用）
RenderGraph::TextureHandle importTexture(const TextureDesc&, VkImage, VkImageView,
                                         VkImageLayout initialLayout); // 导入外部资源，生命周期归调用方
VkImage/VkImageView/VkFormat/VkExtent2D/const TextureDesc& getImage/getView/getFormat/getExtent/getDesc(h);

// Pass 声明（全部返回 Pass&，可链式）
read(t, stage = FRAGMENT_SHADER, access = SHADER_SAMPLED_READ)
readStorage(t, stage = COMPUTE_SHADER, access = SHADER_STORAGE_READ)
writeColor(t, loadOp = LOAD, clear = {}, storeOp = STORE)
writeDepth(t, loadOp = CLEAR, clear = {}, storeOp = STORE)
writeStorage(t, stage = COMPUTE_SHADER, access = SHADER_STORAGE_WRITE)
transitionTo(t, layout, stage = NONE, access = NONE)   // 只转布局，不当渲染目标
execute(std::function<void(VkCommandBuffer)>)
setEnabled(bool)      // 逐 pass 开关（ImGui 用这个）
setExtent(VkExtent2D) // 不指定则取第一个附件的尺寸
```

### 7.3 为什么是「线性」而不是完整 DAG

- **Pass 严格按声明顺序执行**，不做重排 / 内存别名化 / 自动剔除。
- 理由：手写屏障的 90% 痛点（布局转换、读后写、写后读）在"顺序执行 + 记录上一次访问状态"这个模型下就已经解决了；而完整 DAG 需要处理拓扑排序、生命周期分析、跨帧资源池化、别名内存布局 —— 复杂度和调试难度高一个量级，而收益要等场景规模真的上来才体现。
- 结构上留了扩展位：`TextureEntry` 已经记录了 `layout/stage/access`，`passes` 已经是独立对象，将来要加自动剔除或别名化不用推倒重来。

### 7.4 屏障是怎么推导出来的（`RenderGraph::emitBarrierIfNeeded`）

每张图（`TextureEntry`）记住**上一次被谁、以什么布局/阶段/访问类型访问过**：

```cpp
const bool isRead = (acc.access & kWriteBits) == 0;
const bool sameState = (e.layout == acc.layout && e.stage == acc.stage && e.access == acc.access);
if (sameState && isRead) return;     // 连续两次完全相同的只读访问：不用屏障
// 其余（含 write→write、以及任何状态变化）一律发屏障 —— 偏保守，但保证正确
VkImageMemoryBarrier2 b{ ... b.oldLayout = e.layout; b.newLayout = acc.layout; ... };
vkCmdPipelineBarrier2(cmd, &dep);
```

`kWriteBits` = `SHADER_WRITE | COLOR_ATTACHMENT_WRITE | DEPTH_STENCIL_ATTACHMENT_WRITE | TRANSFER_WRITE | HOST_WRITE`。
`srcStageMask/srcAccessMask` 直接沿用上一次记录 —— 这就是"**漏声明就等于漏同步**"的根源（见 ARCHITECTURE.md 同步模型一节）。

同一个 Pass 里对**同一张图**声明了多次访问时（例如 Bloom 上采样既采样又写同一张图），`endFrame` 只保留**最后一次**声明（按声明顺序原地覆盖），因为屏障只需要保证"进入这个 Pass 时资源处于正确布局"，中间那次转换是白发的。
另外有一条"空屏障"保护：如果本次访问不需要换布局，而且**前后两侧的 stage/access 都是 `NONE`**，就静默跳过 —— 这种屏障在 Vulkan 里没有任何依赖项（sync1 要求 src 或 dst 至少一项非 `NONE`），留着只会在特定 Pass 顺序下变成验证层报错。

### 7.5 声明合法性校验（D-2 新增）

"声明即同步"的代价是**漏声明不会报错，只会出怪画面**。所以 `endFrame()` 在录制前会先把最常见的几类错误挑出来，直接抛异常并**报出纹理名和 Pass 名**：

| 检查 | 触发条件 |
|------|---------|
| 句柄越界 | `handle.id >= textures.size()` |
| 颜色附件的 usage 不对 | 声明成颜色附件，但创建时没给 `COLOR_ATTACHMENT` |
| 深度附件的 usage 不对 | 声明成深度附件，但创建时没给 `DEPTH_STENCIL_ATTACHMENT` |
| 采样源的 usage 不对 | 访问位含 `SHADER_SAMPLED_READ`，但创建时没给 `SAMPLED` |
| 存储图像的 usage 不对 | 访问位含 `SHADER_STORAGE_READ/WRITE`，但创建时没给 `STORAGE` |

这类错误如果不在这里拦，表现是"验证层报一句看不懂的 layout/usage 不符 + 花屏"，而且**要等到那一帧真的录到才炸**。现在它在第一帧就带着名字抛出来。加新 Pass 时如果看到这几条异常，答案通常就在 `createTexture` / `importTexture` 的 `usage` 参数里。

### 7.5b 语义校验：瞬时资源每帧必须有人写（W2 新增）

上一节查的是"声明本身合不合法"。这一节查的是另一件事，也是更难查的一件：

> **下游 Pass 读的这张瞬时图，这一帧到底有没有人写过？**

渲染图保证的是**同步**正确性（谁读谁写 → 屏障）。它管不了**语义**正确性 —— 一张
`createTexture` 出来的瞬时图每帧内容都会重置，如果写它的 Pass 被跳过了，下游读到的
就是未定义内容。而**验证层对此一言不发**：从 `UNDEFINED` 布局转出来在同步上完全合法。

`RenderGraph::validateWriteCoverage()` 在 `endFrame()` 里、**录制任何命令之前**执行：

1. 按声明顺序遍历所有**已启用**的 Pass（这就是"线性渲染图"的好处 —— 顺序遍历就能得出准确结论，不需要真正的 DAG 分析）；
2. 维护"已被写过"集合。同一个 Pass 里既采样又写同一张图是允许的（Bloom 上采样），所以**先登记本 Pass 的写，再看本 Pass 的读**；
3. 遇到读但没写过 → 抛异常。

报错会指名道姓，并指出**本来该由谁写**：

```
RenderGraph 语义错误: Pass "Lighting" 读取了瞬时纹理 "SSAO"，但这一帧到目前为止没有任何 Pass 写过它。
  它本来是由 Pass "SSAO（本帧被 setEnabled(false) 跳过）" 写的。
  瞬时资源每帧内容都会重置，读到的将是未定义内容 —— 注意验证层对此不会报错
  （从 UNDEFINED 布局转出来本身是合法的），只能靠这条语义检查拦住。
  两种修法：
    ① 让写入者照常执行，把开关下沉到着色器（SSAOPass 的做法）；
    ② 调用方按帧如实声明：不需要这张图时就不要 read 它（PresentPass 对 Bloom 的做法）。
```

**常驻资源豁免**：`TextureDesc::persistent == true` 的图允许整帧只读（IBL 烘焙结果就是这种）。
`createTexture` 从 `desc.persistent` 取，`importTexture` 则把"初始布局 ≠ `UNDEFINED` ⇒ 常驻"的结论**回写进 `desc.persistent`** —— 让"是否常驻"只有一个事实来源。第一版校验就是因为这两个字段各说各话，把 `Environment`/IBL 误判成"没人写的瞬时资源"。

> **这条校验的价值不在它抓到了什么，而在它把一条本来靠人记的规则变成了机器守的规则。**
> 加新 Pass 时不用再想"我关掉它会不会让下游读到垃圾" —— 关掉就会报错。

### 7.6 资源归属：`createTexture` vs `importTexture`

| | `createTexture` | `importTexture` |
|---|---|---|
| 谁创建 | 渲染图（VMA） | 调用方（GBuffer / Swapchain / Texture / CubemapTexture / IBLBaker） |
| 谁销毁 | `~RenderGraph()`（`owned == true`） | 调用方 |
| `persistent` | 由 `desc.persistent` 指定（默认 `false`） | **由 `initialLayout` 推出**，并回写进 `desc.persistent` |
| `resetEachFrame` | `!desc.persistent`（即默认 `true`） | 同上 |

`persistent = true` 是给**跨帧常驻的只读资源**用的（IBL cubemap / BRDF LUT / 环境图）。每帧 `beginFrame` 会把 `resetEachFrame == true` 的资源布局记录复位成 `UNDEFINED`；如果 IBL cubemap 也被复位，渲染图会以为"内容未定义"，下一帧的屏障就带着 `oldLayout = UNDEFINED`，语义上等于丢弃内容。

它同时是 7.5b 那条语义校验的豁免依据：常驻资源允许整帧没有写入者。

### 7.7 `presentTransition` 为什么要应用显式声明

```cpp
void RenderGraph::presentTransition(TextureHandle handle)
{
    Pass& p = addGraphicsPass("PresentTransition");
    p.transitionTo(handle, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);   // 无附件 → 只发屏障
}
```

**渲染图不应该知道"哪张图要被 present"** —— 那是应用层的知识（交换链属于 `Swapchain`，每帧按 `imageIndex` 选一张）。把它做成显式调用，好处是：
1. 渲染图保持对 present 的零知识，将来接离屏渲染 / 多窗口不用改渲染图；
2. 转换的时机由应用控制（必须在**本帧最后一个 Pass 之后**）。

---

## 8. 着色器一览

编译方式：`glslangValidator -V`（GLSL 450 → SPIR-V），输出到 `build/shaders/*.spv`，运行时用 `SENTINEL_SHADER_DIR` 宏拼路径读取。

| 文件 | 用于 | 说明 |
|------|------|------|
| `mrt.vert` | GeometrySystem `geometryPipeline` | 世界位置/法线/UV 输出，`mat3(normalMatrix)` 变换法线 |
| `mrt.frag` | 同上 | 写 4 个 G-Buffer 附件；偏导数法线贴图（**当前有意关闭**，`getNormalFromMap()` 保留待用） |
| `grid_mrt.vert/.frag` | GeometrySystem `gridPipeline` | 程序化棋盘格无限网格，`fwidth` 抗锯齿 + 距离淡出。**当前没有实体把 `RenderTagComponent::isGrid` 设为 true，所以这条管线不会被触发** |
| `skybox_mrt.vert/.frag` | GeometrySystem `skyboxPipeline` | `gl_Position = pos.xyww` 深度欺骗 + 跟随相机；albedo 写线性 HDR，normal 写 0 |
| `fullscreen.vert` | SSAOPass / BloomPass / PresentPass | 顶点索引生成全屏三角形，无顶点缓冲，输出 `[0,1]` UV |
| `ssao.frag` | SSAOPass | **视图空间**半球采样（TBN 用 4×4 噪声旋转）+ range check；输出单通道遮蔽（1.0 = 无遮蔽） |
| `lighting.vert` | LightingSystem | 同 fullscreen（自己一份，没复用 `fullscreen.vert`） |
| `lighting.frag` | LightingSystem | Cook-Torrance GGX + Smith + Schlick；方向光 + 最多 100 点光；**split-sum IBL**（irradiance × albedo + prefiltered × BRDF-LUT）；`ambient *= ssao`；**只输出线性 HDR，不做 tonemap** |
| `bloom_bright.frag` | BloomPass | Rec.709 亮度 + **soft knee**（避免阈值硬边），输出 `c * weight * intensity` |
| `bloom_down.frag` | BloomPass | 13 抽样 dual-filter 降采样；首级打开 **Karis 加权**抑制萤火虫 |
| `bloom_up.frag` | BloomPass | 3×3 帐篷滤波；配合管线的 `ONE/ONE` 加法混合叠加到细一级 |
| `present.frag` | PresentPass | `hdr + bloom * intensity` → 曝光 → **ACES Filmic（Narkowicz）** → `pow(1/2.2)` → 交换链 |
| `ibl_capture.vert` | IBLBaker（4 条管线共用） | 全屏三角形（立方体面方向由 push constant 的 `faceIndex` 推出） |
| `ibl_equirect_to_cube.frag` | IBLBaker | 等距柱状 → 立方体（6 面） |
| `ibl_irradiance.frag` | IBLBaker | 半球余弦卷积（`sampleDelta = 0.025`，固定采样数） |
| `ibl_prefilter.frag` | IBLBaker | GGX 重要性采样 + Hammersley + 采样立体角估算 mip |
| `ibl_brdf_lut.frag` | IBLBaker | split-sum 第二项积分表，(NdotV, roughness) → (A, B)，1024 采样 |
| ~~`light_cube.vert/.frag`~~ | **没人引用** | 光源调试可视化，历史残留 |
| ~~`shader.vert/.frag`~~ | **没人引用** | 更早期的教学 shader，历史残留 |

> `shaders/` 目录里还躺着 `vert.spv` / `frag.spv` / `light_cube.*.spv` / `shader.*.spv` 等**源目录里的 .spv**。
> 构建产物一律写到 `build/shaders/`，这些是早期误留在源码目录里的垃圾，可以删。

---

## 9. 核心类 API 速查

### FirstApp（`Core/Application.h/.cpp`）

引擎总控。构造函数里**顺序是语义的一部分**：

1. `initWindow()` → `Device` → `Swapchain` → `Renderer` → `GBuffer`
2. `RenderGraph` + `RenderResources`（**必须在各 Pass 之前** —— Pass 的 `init()` 会往里注册资源）
3. `DescriptorManager`（**必须在 Scene 之前**：glTF 材质在加载时就要分配描述符槽位）
4. `Scene::init()` + `registerCallbacks()` → `loadAllPBRTextures()`
5. `IBLBaker::bake()`（**必须在 `buildGlobalSet` 之前**：烘焙结果要写进 Set 0）
6. `createUniformBuffers()` → `initMaterials()`
7. 构造 5 个 Pass → `renderPipeline.initAll()`（Geometry → SSAO → Lighting → Bloom → Present）
8. 导入跨帧常驻资源（环境图 + IBL 三件套，`initialLayout = SHADER_READ_ONLY`）
9. `descriptorManager->buildGlobalSet(...)`
10. `ImGuiSystem`（**必须在 PresentPass 之后**：要 `setImGuiSystem()` 把指针交给它）

**成员**：window / vulkanDevice / vulkanSwapchain / renderer / gBuffer / descriptorManager / scene / imguiSystem / iblBaker / renderPipeline / renderGraph / renderResources / globalUboBuffer+uboMapped / environmentTex / 时间戳。

**公开方法**：`run()`、`setBloomEnabled(bool)`、`setSSAOEnabled(bool)`、`runHeadlessSelfTest(int frames)`（后三个是 D-2 新增的无人值守自检入口，见 3.6）。

**私有方法**：`initWindow()`、`loadAllPBRTextures()`、`createUniformBuffers()`、`initMaterials()`、`update(dt)`、`render(dt)`。

### RenderGraph（`RHI/RenderGraph.h`）

见 7.2。补充两个细节：
- `Pass::writeDepth` 里有一句"如果 clear 值的 depth==0 且 stencil==0，就改成 `{1.0f, 0}`"—— 这是给"没显式给清屏值"的情况兜底。**副作用**：想真的把深度清成 0.0 的人会被静默改写。
- `setPassEnabled(name, enabled)` 这个按名字查的接口**实际不可用**：`endFrame()` 结束时会把 `passes` 全部清空，帧与帧之间 `passes` 是空的，查不到任何东西。现在真正生效的是 `Pass::setEnabled()`（各 Pass 在 `addToGraph` 里自己调）。

### IRenderPass（`Render/IRenderPass.h`）

```cpp
virtual void init() = 0;                                       // 建资源、建管线
virtual void addToGraph(RenderGraph&, FrameInfo&) = 0;          // 声明本帧工作
virtual std::string getName() const = 0;
```

注意**没有** `execute(VkCommandBuffer, FrameInfo&)`、也**没有** `onResize()` 了 —— 旧的 `PostProcessPass` 基类已在 D-1(b) 删除。

### RenderPipeline（`Render/RenderPipeline.h`）

`addPass(unique_ptr<IRenderPass>)` / `initAll()` / `registerAll(graph, frameInfo)` / `getPasses()` / `findPass(name)`。

### RenderResources（`Render/RenderResources.h`）

帧内资源的集中登记处：`gbPosition/gbNormal/gbAlbedo/gbPbr/gbDepth`、`ssao`、`hdrScene`、`bloomLevels`、`bloomResult`、`hdrSampler`、`environment/irradiance/prefiltered/brdfLut`、`swapchainImages`。
**它同时也是同步关系的事实来源** —— 各 Pass 从这里取 handle 去声明 read/write，谁读了谁写了全看这些声明。

### DescriptorManager（`Core/DescriptorManager.h`）

```cpp
void init(Device&, uint32_t maxMaterials = 256);
void cleanup();
VkDescriptorSetLayout getGlobalSetLayout() const;
VkDescriptorSetLayout getMaterialSetLayout() const;
VkDescriptorSetLayout createSetLayout(const std::vector<VkDescriptorSetLayoutBinding>&);

void buildGlobalSet(VkBuffer ubo, const VkDescriptorImageInfo& env, const GBuffer&,
                    VkSampler ssaoSampler, VkImageView ssaoView,
                    const VkDescriptorImageInfo& irradiance,
                    const VkDescriptorImageInfo& prefiltered,
                    const VkDescriptorImageInfo& brdfLut);       // 全生命周期只调一次
VkDescriptorSet getGlobalSet() const;

uint32_t allocateMaterialSet(albedo, normal, metallic, roughness);
uint32_t allocateMaterialSet(const Material&);
void     releaseMaterialSet(uint32_t index);                     // 越界/重复归还安全忽略
VkDescriptorSet getMaterialSet(uint32_t index) const;
uint32_t getMaterialSlotCapacity() const;  uint32_t getMaterialSlotUsed() const;

VkDescriptorSet allocateSet(VkDescriptorSetLayout);
void writeSet(VkDescriptorSet, const std::vector<VkWriteDescriptorSet>&);
VkDescriptorPool getPool() const;
```

### MaterialSystem（`Core/MaterialSystem.h`）

```cpp
void initDefaultTextures(Device&, VkCommandPool);                            // 1x1 白 / 黑 / 平法线
void buildMaterialDescriptor(shared_ptr<Material>, DescriptorManager&);      // 分配槽位并回写 descriptorSetIndex
shared_ptr<Material> getOrCreateMaterial(const string& name);                // 按名字去重
shared_ptr<Texture> loadTexture(path, Device&, VkCommandPool, bool srgb=false);
shared_ptr<Texture> getTexture(path) const;
void cleanup();
```

`buildMaterialDescriptor` **每次调用都会消耗一个新槽位**，所以在重复加载场景时要先用 `getOrCreateMaterial` 去重（`GLTFLoader` 就是这么做的）。

### Material（`Core/Material.h`）

`name` / `descriptorSetIndex`（哨兵 `kInvalidMaterialSlot`）/ `baseColorFactor` / `metallicFactor` / `roughnessFactor` / `alphaCutoff` / `alphaMode`（`OPAQUE_MODE` / `MASK_MODE` / `BLEND_MODE`）/ 5 个纹理指针（albedo / normal / metallicRoughness / emissive / ao）。
`getAlbedoInfo()` 等"安全 getter"在缺贴图时返回静态兜底 `VkDescriptorImageInfo`（白 / 黑 / 平法线）。

> ⚠️ `alphaMode` / `alphaCutoff` / `emissiveMap` / `aoMap` 目前**只在结构体里存在**，`mrt.frag` 完全没有使用它们（没有 discard、没有自发光、没有 AO）。

### Scene（`Scene/Scene.h`）

```cpp
void init(Device&, VkCommandPool, DescriptorManager&);
void update(float dt, GLFWwindow*, float aspectRatio);   // camera + hierarchy + spatial culling
void cleanup();
entt::entity createEntity();  void destroyEntity(entt::entity);
void setParent(entt::entity child, entt::entity parent);
entt::registry& getRegistry();
const std::vector<entt::entity>& getVisibleEntities() const;
TransformComponent& getCameraTransform();
VulkanCamera& getCamera();  CameraController& getCameraController();
EngineSettings& getSettings();  RenderTelemetry& getTelemetry();
MaterialSystem& getMaterialSystem();
static bool selfTest();   // hierarchy + spatial index regression
```

场景装配顺序：`debug_cube.obj`（球体，历史命名）→ Sponza（`kLoadSponza = true` 时）→ 失败则回退 DamagedHelmet → 最后永远追加一个 `isSkybox` 的 `sphere.obj`（缩放 0.1，跟随相机）。

### CameraController（`Scene/CameraController.h`）—— 参数表

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `moveSpeed` | 5.0 | FPS 模式移动速度（单位/秒） |
| `sprintMultiplier` | 3.0 | 按住 Shift 的倍率 |
| `lookSensitivity` | **0.01** | 鼠标灵敏度（旧文档写的 0.15 是错的） |
| `orbitSpeed` | 2.0 | Orbit 模式旋转速度 |
| `scrollSpeed` | 10.0 | 滚轮缩放速度 |
| `smoothEnabled` | true | 阻尼开关 |
| `smoothFactor` | 10.0 | 阻尼系数（越大越跟手） |
| `minDistance` / `maxDistance` | 0.5 / 500.0 | Orbit 距离夹取范围 |
| `mode` | `Mode::FPS` | 右键拖拽切 FPS、中键拖拽切 Orbit |
| `keys` | `KeyMappings` | `WASD` + `E/Q` 上下 + `LeftShift` 冲刺（`KeyMappings` 全部可在 ImGui 里改） |
| 内部状态 | `yaw` / `pitch` / `distance` / `focusPoint` / `smoothPosition` / `smoothYaw` / `smoothPitch` | 私有 |

公开方法：`processPCInput(window, dt, cameraTransform)`、`processAndroidTouchInput()`（**空桩**）、`onScroll(xoff, yoff)`、`registerCallbacks(window)`、`setMode()` / `getMode()` / `setFocusPoint()`。

### FrameInfo（`Core/FrameInfo.h`）—— 每帧数据总线

```cpp
struct FrameInfo {
    uint32_t frameInFlightIndex;
    uint32_t swapchainImageIndex;
    uint64_t frameNumber;
    float frameTime;
    VkCommandBuffer commandBuffer;
    EngineSettings& settings;  RenderTelemetry& telemetry;
    entt::registry& registry;
    const std::vector<entt::entity>& visibleEntities;
    glm::mat4 projectionMatrix, viewMatrix;
    VkBuffer globalUboBuffer;
    DescriptorManager* descriptorManager;
    TransformComponent* cameraTransform;  CameraController* cameraController;
};
```

同文件还定义了 `PointLight`、`GlobalUbo`（**带 `static_assert(offsetof(...))` 的 std140 布局校验**）、`SimplePushConstantData`、`EngineSettings`、`RenderTelemetry`，以及 `#define MAX_POINT_LIGHTS 100` / `WIDTH 1920` / `HEIGHT 1080`。

### EngineSettings（ImGui 面板可调的全部参数）

| 分组 | 字段 | 默认 | ImGui 控件 |
|------|------|------|-----------|
| SSAO | `ssaoEnabled` / `ssaoRadius` / `ssaoBias` / `ssaoKernelSize` | true / 0.5 / 0.025 / 16 | Checkbox + 3 个 Slider（kernel 4~64） |
| 预留 | `ssrEnabled` / `shadowEnabled` | false / false | **面板里没有暴露** |
| 光照 | `directionalLightDir` / `directionalLightColor` | (-1,-1,1,0) / (1,1,1,1) | SliderFloat3 + ColorEdit3 |
| 后效 | `exposure` | 1.0 | SliderFloat 0.1~5.0（作用在 PresentPass） |
| Bloom | `bloomEnabled` / `bloomThreshold` / `bloomKnee` / `bloomIntensity` | true / **2.2** / 0.5 / **0.12** | Checkbox + 3 个 Slider（阈值 0~4，膝盖 0.01~2，强度 0~0.5） |

`exposure == 1.0` 之所以是"物理正确"的基准值：split-sum IBL 的能量已经归一化（irradiance 存的是 E/π，specular 走 prefiltered × BRDF-LUT），所以 1.0 直接就是合理画面。

**Bloom 的三个默认值是实测扫出来的，不是拍脑袋**（D-2 提交记录）：

| 阈值 / 强度 | 结果 |
|------------|------|
| 1.0 / 0.04 | 几乎看不出辉光（变化 > 8 的像素只占 17%） |
| 1.0 / 0.25 | 整片天空超过阈值，画面发灰像蒙了一层雾 |
| 1.8 / 0.30 | 能看出辉光，但把过曝的墙面整片糊白 |
| 0.2 / 0.80 | 极端值，60.9% 像素过曝 —— 用来确认链路是通的 |
| **2.2 / 0.5 / 0.12（最终）** | 收敛成"围绕强光的一圈柔和外溢" |

调参时**一定要配直方图看**：Bloom 的失败模式有两种截然相反的观感 —— "太弱"（看不出）和"阈值低于天空亮度"（整片假雾），只靠肉眼看截图很容易把后者当成"辉光效果很好"。

### EngineConfig（`Core/EngineConfig.h/.cpp`，W1 新增）—— 运行时配置持久化

**为什么有这个东西**：D-2 调 Bloom 参数时，每换一组值都要走"改默认值 → 重编译 → 启动 → 截屏 → 统计"，一轮几分钟，而可用区间是靠扫参找到的（试了四组）。面板本来就能实时改这些值，只是改完不落盘。

```cpp
bool load(EngineSettings& out);       // 文件不存在/语法错/字段类型不对都不抛异常
bool load(CameraState& out);
bool save(const EngineSettings&);     // 两次 save 都是"读回已有内容再替换自己那一段"
bool save(const CameraState&);        // -> 两者不会互相覆盖
static const std::string& getLastStatus();     // 供 UI 显示（ASCII！见下）
static const std::string& getConfigPath();     // 绝对路径
static std::string getConfigFileName();        // 只取文件名（UI 用，避免撑宽面板）
```

| 事项 | 结果 |
|------|------|
| 文件位置 | `SENTINEL_CONFIG_PATH`（CMake 注入）= `<工程根>/config/engine.json` |
| 解析库 | nlohmann/json 3.10.4，**已经躺在 `3rdParty/tinygltf/json.hpp`**（tinygltf 拉的），不额外引依赖、不联网 |
| 落盘时机 | 关窗退出（析构里调，所以自检/异常退出也覆盖）+ 面板 Save 按钮 |
| 原子写 | 先写 `.tmp` 再 rename。直接覆盖写的话，写到一半崩溃会留下语法坏掉的配置 |
| 自检免疫 | 自检会把 bloom/ssao 关掉，所以自检模式下 `suppressAutoSave` 禁掉退出保存 |

**实测**：只改 json、不重编译，把 `exposure` 从 1.0 改成 2.0 → 过曝像素从 7.5% 涨到 34%。

> ⚠️ **UI 状态文字必须是 ASCII**。`EngineConfig` 的 status 是画在 ImGui 面板上的，而 ImGui 用内置默认字体（ProggyClean）只有 ASCII 字形 —— 写中文会整条渲染成 `???`，看着像坏了。代码注释、控制台输出仍然用中文，只有这一条是给 UI 看的。想上中文得先给 ImGui 挂一份中文字体（字体加载 + 图集重建），那是另一件事。
>
> ⚠️ 面板里显示路径要用 `getConfigFileName()`，不要用 `getConfigPath()`。完整绝对路径五十多个字符会把 ImGui 窗口从 283px 撑到 583px —— ImGui 的窗口宽度是由内容撑开的，而**宽度是"历史最大值"，出现过一次就再也不会缩回来**。所以 `prepareDrawData` 里每帧调了一次 `SetNextWindowSize(0,0)` 让它贴合当前内容。

### VMA 泄漏排查工具（W1 顺带加的）

`vmaDestroyAllocator` 发现有分配没释放时会直接断言崩掉，而断言**只说"有分配没释放"，不说是谁**。为此加了三件套：

| 东西 | 作用 |
|------|------|
| `CMake -DSENTINEL_VMA_LEAK_CHECK=ON` | 打开详细泄漏日志（默认关，会明显变慢） |
| `src/RHI/VmaConfig.h` | 定义 `VMA_LEAK_LOG_FORMAT`（必须在 `vk_mem_alloc.h` 之前） |
| `src/RHI/VmaNaming.h` | `sentinel_vmaNameFlag()` —— 给分配附名字（必须在之后） |
| `Device::reportVmaStats(stage)` | 在析构链各处插调用、对比 `allocationCount`，把范围缩到"两次调用之间被销毁的对象" |

配合使用，W1 那次定位到具体缓冲只花了两轮。用到了就**记得把调用点删掉**（工作区里不留调用点，只留函数）。

### ImGuiSystem（`Core/ImGuiSystem.h`）

```cpp
ImGuiSystem(GLFWwindow*, Device&, VkFormat swapchainFormat, uint32_t imageCount, VkCommandPool);
void newFrame();                        // 在 update() 里调
void prepareDrawData(FrameInfo&);       // 建面板 + ImGui::Render()（纯 CPU，无 GPU 命令）
void renderDrawData(VkCommandBuffer);   // 真正的 vkCmdDraw，由 PresentPass 调
```

**为什么拆成两段**（D-2）：
- `prepareDrawData` 放在 `renderGraph->beginFrame()` **之前** —— 面板里改的值当帧生效，不会有一帧延迟的"滑块发飘"。
- `renderDrawData` 由 `PresentPass` 在自己的动态渲染作用域里调 —— UI 画在最终图像之上。

**面板分区**：Performance / SSAO / Lighting / Bloom / Camera / **Config**（W1 新增，Save + Reload 按钮 + 上次读写结果）。

面板分组：Performance / SSAO / Lighting / Bloom / Camera。

---

## 10. 已踩过的坑清单

这些是**真实踩过并已修复**的坑；改相关代码前先读一遍，能省几个小时。

### 坑 1：CRLF 混杂导致补丁打不进去

工程里同时存在 CRLF 和 LF 的文件（某些文件被不同工具改写过）。用脚本按行替换时，如果模式串用的是 LF 而文件是 CRLF，会**匹配不上却静默成功**。改文件前先确认行尾（`\r\n` 还是 `\n`），或者干脆按"整段唯一子串"替换而不是按行。

### 坑 2：`tiny_gltf.h` 的 include 顺序是硬要求

`tiny_gltf.h` 的**声明部分在 include guard 内**，而**实现部分（`#if defined(TINYGLTF_IMPLEMENTATION)`）在 include guard 之外**。因此必须：

```cpp
#include "Scene/GLTFLoader.h"      // 1) 先包含一次，拿声明（此时宏还没定义）
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>             // 2) 再定义宏包含一次，实现恰好展开一次
```

**反过来写（先定义宏再包含）实现会被展开两次，直接重定义编译失败。**
另外：`TINYGLTF_NO_STB_IMAGE` / `TINYGLTF_NO_STB_IMAGE_WRITE` 由 CMake 全局注入（见 3.3 第 3 点），不要挪到单个 .cpp 里。

### 坑 3：ImGui 动态渲染的字段位置（1.92.6 与老教程不同）

```cpp
init_info.UseDynamicRendering = true;
init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
```

字段在 **`PipelineInfoMain` 下面**，不在顶层。网上大量示例写的是顶层的 `init_info.PipelineRenderingCreateInfo` —— 那是老版本，在这里编译不过。
另外 `pColorAttachmentFormats` 指向的 `swapchainFormat` 必须是**活到 ImGui shutdown 的成员**（`ImGuiSystem::swapchainFormat`），不能是临时变量。

### 坑 4：**绝对不要用 0 当"未分配"哨兵**

`materialSetIndex` / `SubMesh::materialSlot` / `Material::descriptorSetIndex` 统一用 `kInvalidMaterialSlot = UINT32_MAX`。
原来的默认值是 0，导致"没有材质的物体"（天空盒、网格）**悄悄绑到第 0 号材质上** —— 只是因为第 0 号槽恰好已经分配过才没崩。这就是典型的"靠巧合工作"。

同理，`GeometrySystem::render` 里的 `lastBoundSlot` 初值也是 `kInvalidMaterialSlot`，保证第一个子网格一定会真正 bind 一次。

### 坑 5：录制完就销毁 Framebuffer / ImageView 是错的

命令缓冲只是"**录进去**"了，还没提交执行。此时销毁 Framebuffer 会让 `vkCmdBeginRenderPass` 指向已销毁对象 —— 验证层刷屏（而且往往刷在几十帧之后，很难定位）。
`IBLBaker` 的做法是：把新建的 `VkFramebuffer` 攒进 `pendingFramebuffers`，等 `cmdEndSingleTime()`（内部 `vkQueueWaitIdle`）之后再统一释放。

### 坑 6：动态 viewport/scissor 必须在录制时显式设置

所有图形管线都启用了 `VK_DYNAMIC_STATE_VIEWPORT` + `VK_DYNAMIC_STATE_SCISSOR`，这两个状态**不会被烘焙进管线**。忘了设就是未定义行为（验证层 `VUID-vkCmdSetViewport-None-08971` 之类）。
渲染图的 `endFrame()` 会在 `vkCmdBeginRendering` 之后自动调 `cmdSetFullViewportAndScissor()`；但**不走渲染图的录制路径**（`IBLBaker::renderFaces` / `renderBrdfLut`）必须自己调。
ImGui 后端会自己设 viewport/scissor，所以在 PresentPass 里不用补。

### 坑 7：非一致（non-coherent）映射内存必须显式 flush

持久映射的 UBO 不保证落在 `HOST_COHERENT` 内存。`memcpy` 之后必须 `vmaFlushAllocation(...)`，否则 GPU 可能读到上一帧的旧数据。
`vmaFlushAllocation` 对 coherent 内存是空操作，所以可以无条件调用（`Application::render` 和 `SSAOPass::addToGraph` 都这么做）。

### 坑 8：SSAO 的遮挡判断必须在视图空间做

原实现在**世界空间直接比较 z** —— 那只有当相机恰好朝向 -Z 时才成立。相机一转，遮蔽就随朝向乱变（侧面失效、地面出现方向性条纹）。
正确做法：把位置和法线都转到视图空间再比较（`ssao.frag` 里有详细注释）。

### 坑 9：法线贴图 UV 接缝与退化

切线空间法线贴图用偏导数构造 TBN 时，跨越贴图 0~1 边界的像素会算出巨大的 `dFdx(uv)`，TBN 直接爆掉（表现为模型上一条条黑缝）。
`mrt.frag::getNormalFromMap()` 里做了两道拦截：**UV 跨度 > 0.5 直接回退顶点法线**、**行列式 < 1e-5 直接回退**。
（这个函数**当前是有意不调用的**：模型还不能提供可靠的切线，`outNormal` 直接写顶点法线。保留它是为了等材质系统完善后启用。）

### 坑 10：glTF 的 UV 原点与 OBJ 相反

glTF 的 UV 原点在**左上**，与 Vulkan 一致 → 直接用。
OBJ 的 UV 原点在左下 → `Model.cpp` 的 OBJ 路径要做 `1.0 - v` 翻转。
**两条路径的翻转策略不同，别统一。**（`GLTFLoader.cpp` 里对这一点有显式注释。）

### 坑 11：Python 脚本写 C++ 字符串字面量时 `\n` 会被二次转义

用 Python（或任何脚本）批量生成/改写 C++ 源码时，如果日志字符串写成普通字符串，`\n` 会被 Python 先解释成真正的换行符，写进文件后就变成**字面换行**，C++ 编译直接报错（"newline in constant"）。
必须写 `\\n`（raw string 或 `r"..."`），或者干脆不用脚本改源码。
同理要注意 `\t`、`%` 与 Python 格式化、以及中文路径的编码问题。

### 坑 12：`find_package(glfw3)` 与手动 include 路径并存

`CMakeLists.txt` 里既 `find_package(glfw3 REQUIRED)`，又把 `${GLFW_DIR}/include` 塞进 include 目录，同时链接目标写的是 `glfw`。这套组合在"3rdParty 里有源码 + 系统里也装了 glfw3"的机器上能工作，但**依赖顺序脆弱**。换机器如果报 glfw 找不到，优先检查这里。

### 坑 13：新增 `src/*.cpp` 后不重新 configure 会在链接期才炸

见 3.4。这是本工程**最高频**的构建坑。

### 坑 14：「把某个 Pass 关掉」是最容易漏验证层错误的分支

`pass.setEnabled(false)` 一跳过，这个 Pass 的**输出图整帧就没人写**了 —— 它的布局一直停在 `UNDEFINED`，而后面照样有 Pass 声明要采样它。
这类问题在"正常跑"的分支里**永远测不到**，而 ImGui 的复选框又没法在无人值守的流程里点。

D-2 的应对方式是把开关暴露给命令行（`--selftest [--no-bloom] [--no-ssao]`，见 3.6），让它进自动回归。
**加新 Pass 时请照这个模式做**：只要是"可以被关掉"或"有条件跳过"的 Pass，就给它一个命令行开关 + 固定帧数跑法，否则那条分支就是验证盲区。

> **这条坑不是假设，是已经踩到的**：SSAO 就正好踩在上面 —— 而且它更进一步，
> `pass.setEnabled(false)` 跳过后，`lighting.frag` 里的 `ambient *= ssao` 是**无条件**执行的，
> 于是环境光被乘上一片未定义内容。修复见第 12 节第 2 条（`3fed268`）。
> 值得注意的是：**验证层对这个洞一言不发**，所以"验证层 0 字节"不等于"语义正确"。
>
> W2 之后这条坑有了机器检查：`RenderGraph::validateWriteCoverage()` 会在
> `endFrame` 里按声明顺序扫一遍，"有人读的瞬时图没人写"直接抛异常并指名道姓
>（还会告诉你它本来该由哪个 Pass 写、那个 Pass 是不是被跳过了）。见 7.5。

### 坑 15：销毁顺序不能靠成员声明顺序表达（W1 挖出来的真 bug）

`FirstApp` 里 `renderPipeline` 是**值成员**，声明得比 `vulkanDevice` 晚 —— 于是成员逆序析构时它排在 `vulkanDevice` **之后**销毁。结果：Pass 持有的 VMA 分配（`SSAOPass` 的 UBO 等）在 `vmaDestroyAllocator` 之后才被释放，直接触发 VMA 的 `Some allocations were not freed before destruction of this memory block!` 断言崩溃。

**它为什么藏了这么久**：验证脚本一直用 `subprocess.terminate()` 杀进程，而 Windows 的 `TerminateProcess` **根本不跑析构函数**。于是"退出路径"从来没被验证过 —— 那句"验证层 0 字节"一直是**不完整**的结论。

教训有两条，第二条比第一条重要：

1. **别依赖成员声明顺序表达销毁依赖**。有人插一个新成员就会悄悄破坏它，而且编译器一声不吭。现在 `~FirstApp` 把顺序全显式写出来：Pass → 依赖窗口的（ImGui / Swapchain）→ 其余 GPU 资源 → Device → 窗口。为此给 `RenderPipeline` 加了 `clearAll()`。
2. **验证脚本的"成功退出"方式会决定你验证到了什么**。改截图脚本时必须用 `WM_CLOSE` 让主循环自己退出（`PostMessageW(hwnd, WM_CLOSE, 0, 0)`），不能 `terminate()` —— 否则析构、保存、清理这些路径全都测不到。

### 坑 16：A/B 截屏之前，先确认两张图都是"本次运行"产出的

D-3 收尾时对比出"0.83% 像素不同"，我差点当成回归去查。实际原因是**上一次的截图文件是旧的** —— 那一轮运行窗口位置和当次不一致，客户区底部把窗口标题栏一起截了进去。

现象很好认，但很容易误判：几十个像素从 `[24,25,26]` 跳到 `[239,239,239]` —— 那不是渲染差异，是截图错位（暗的是标题栏，亮的是被它盖住的画面）。

**判据**：同构建连跑两次，排除 UI 面板后差异应该是 **0.000 / 100% 相同**。先拿到这个噪声下限，再去比不同构建 —— 否则你分不清"改动的效果"和"截图的问题"。

> 相关：`AI_Readme` 第 11 节第 4 步还要求"确认窗口真的到了前台"，那是另一个方向的同一类问题。

---

## 11. 验证工作流（每次改完都走一遍）

### 第 1 步：构建必须零警告

```powershell
cmake -S . -B build
cmake --build build --config Debug 2>&1 | Tee-Object build.log
Select-String -Path build.log -Pattern "warning" -CaseSensitive:$false
```

**0 警告是硬要求**，不是"看着办"。原因：验证层的真错误经常混在一堆警告里被忽略；一旦默认容忍警告，就再也分不清"新引入的"和"历史遗留的"。

### 第 2 步：跑起来看验证层，**stderr 必须 0 字节**

Debug 构建默认开启 `VK_LAYER_KHRONOS_validation`（`Device.h`：`#ifdef NDEBUG → false #else true`），消息走 `debugCallback` → `std::cerr`。

```powershell
.\build\Debug\VulkanEngine.exe 1>build\stdout.txt 2>build\stderr.txt
(Get-Item build\stderr.txt).Length      # 必须是 0
```

> 🔴 **必须用"关窗口"结束它，不能用 `TerminateProcess` / `taskkill /F`。**
> 见坑 15：`TerminateProcess` 不跑析构函数，于是**退出路径（析构、清理、保存配置）
> 完全没被验证到**。W1 就是改成正常关窗之后才炸出 VMA 泄漏的 ——
> 在此之前那句"验证层 0 字节"一直是个不完整的结论。
>
> 自动化脚本里用 `user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)`，然后等进程自己退出。

**必须让它自然跑一会儿再关窗口**（至少跨过几帧）—— 很多同步/生命周期错误只在特定帧才触发。
顺带看 stdout 里的 `[Perf]` 行：

```
[Perf] <FPS> FPS | draw calls <n> | triangles <n> | vertices <n>
```

每 5 秒一行。它同时是"引擎还活着、还在出帧"的心跳 —— 无头验证时没有它就无法区分"渲染正常"和"卡死在第一帧"。

> ⚠️ 因为验证层和普通日志都走 stderr，"stderr 0 字节"实际上**顺带**要求了"没有 `std::cerr` 输出"。`GLTFLoader` 的缺失法线警告、`GeometrySystem` 的"没有有效材质槽位"都会污染它。看到 stderr 非空时，先分清是验证层还是自己的日志。

### 第 3 步：关掉 Pass 的分支也要过一遍（D-2 新增）

```powershell
# 四种组合都要是 0 字节
.\build\Debug\VulkanEngine.exe --selftest 300 1>build\o1.txt 2>build\e1.txt
.\build\Debug\VulkanEngine.exe --selftest 300 --no-bloom         1>build\o2.txt 2>build\e2.txt
.\build\Debug\VulkanEngine.exe --selftest 300 --no-ssao          1>build\o3.txt 2>build\e3.txt
.\build\Debug\VulkanEngine.exe --selftest 300 --no-bloom --no-ssao 1>build\o4.txt 2>build\e4.txt
Get-Item build\e1.txt,build\e2.txt,build\e3.txt,build\e4.txt | Select-Object Name,Length
```

**为什么单列一步**：见坑 14。"某个 Pass 被关掉"和"正常跑"是两条完全不同的验证路径，而它恰恰是最容易漏的一条。
**而且这一步只能证明"验证层没话说"，不能证明"语义正确"** —— SSAO 那个洞（第 12 节第 2 条）在这条命令下也是 0 字节。

W2 之后语义正确性有了机器检查，所以还要加一步**反面用例**：故意把写入者跳过，断言渲染图确实报错。

```powershell
# 反面用例：模拟"关掉 SSAO 就跳过整个 Pass"（也就是 3fed268 修掉的那个错误写法）
$env:SENTINEL_SIMULATE_SSAO_SKIP = '1'
.\build\Debug\VulkanEngine.exe --selftest 5 2>build\sim.txt
Get-Content build\sim.txt -Encoding UTF8     # 必须出现 "RenderGraph 语义错误"
Remove-Item Env:\SENTINEL_SIMULATE_SSAO_SKIP
```

这条用例的价值在于：它证明校验**真的会拦**，而不是只写在注释里当传说。
只测"正常路径不报错"是不够的 —— 一个永远不报错的校验和一个不存在的校验，测起来一模一样。

### 第 4 步：截屏直方图回归

对着**同一机位**（相机初始位姿，鼠标移出窗口）截屏，比较三个数字：

| 指标 | D-1(b) 基线 | D-2 基线（提交时实测） | 判定阈值 |
|------|------------|----------------------|---------|
| 全屏均值 RGB | (183.5, 186.0, 186.0) | (182.3, 184.5, 184.6) | 漂移 > ~2 才值得查 |
| 暗部占比 | 12.7% | 13.7% | 变化 > ~1 个百分点 |
| 过曝占比 | 7.1% | 7.5% | 变化 > ~1 个百分点 |

> ⚠️ **截图会包含 ImGui 面板**，而面板内容会随代码变化（W1 加了 Config 一节，面板从 283px 变宽、变高）。
> 所以逐像素对照**必须把面板区域排除掉**，否则会得出"渲染回归了"的错误结论 ——
> 那是 UI 的差异，不是渲染的差异。W1/W2 交付时排除面板后是 **100% 像素相同（均值 0.000）**。
>
> ⚠️ 另一个坑：截图前必须确认窗口**真的到了前台**（`GetForegroundWindow() == hwnd`），
> 否则会截到桌面或别的窗口 —— 统计出来的数字看起来完全正常，其实毫无意义。
> Windows 有前台锁定（后台进程调 `SetForegroundWindow` 会被静默忽略），
> 需要用 `AttachThreadInput` 临时挂到当前前台窗口的输入队列上才抢得到。

判读经验：
- **均值大幅上升 + 过曝暴涨** → 多半是色彩空间搞错了（双重 gamma、或在 sRGB 附件上又手动 `pow`）。
- **天窗边缘出现硬边** → Bloom 的 soft knee 没生效。
- **整块天空蒙一层雾** → Bloom 阈值低于天空亮度。两次踩过这个坑：① Albedo 用 8bit 时天空被截断到 1.0（恰好超过阈值）；② 天空盒在 G-Buffer 阶段自己做了 ACES+gamma，被 PresentPass 二次 tonemap。
- **过曝从 7% 跳到 95%** → IBL 能量没归一化（老 IBL 实现的问题，split-sum 修掉了）。
- **逐像素对照**：真正的回归验证是"同机位 D-2 vs 当前"逐像素比。D-2 提交时 76% 的像素差 ≤ 8，差异集中在"天窗强光外溢的一圈"和天空色调 —— **差异要能解释成"这次改的就是这里"**，否则就是回归。

### 第 5 步（可选）：单独看某一级

`RenderResources::bloomLevels` 是**故意暴露出来**的：想确认辉光是哪一级贡献的，临时改 `PresentPass` 的 binding 1 指向 `bloomLevels[i]` 即可。接自动曝光（D-3 之后）也会用到它。

---

## 12. 当前状态与待办

### 已完成（git 里程碑）

| 里程碑 | 内容 | 是否已提交 |
|--------|------|-----------|
| baseline | 修改前的原始状态（taught-style 代码） | ✅ `ade374c` |
| A | Vulkan 正确性修复（动态状态、视图空间 SSAO、法线接缝……） | ✅ `8a6406a` |
| B-prep | 材质槽位可回收 + `Model` 子网格 + 材质系统接通渲染循环 + `kInvalidMaterialSlot` 哨兵 | ✅ `64dbb62` |
| B | 接入 tinygltf，加载并渲染 Sponza（几何烘焙到世界空间 + 逐子网格材质） | ✅ `7146077` |
| C | IBL 预过滤：split-sum 近似（irradiance cubemap / GGX prefiltered / BRDF LUT），画面质量质变 | ✅ `477908f` |
| D-1(a) | 引入 EnTT/spdlog 依赖、升到 **Vulkan 1.3**（dynamicRendering + sync2）、落地**线性渲染图** | ✅ `0cf04ee` |
| D-1(b) | 全部 Pass 迁移到动态渲染 + 渲染图；删除 `PostProcessPass`；交换链布局转换交给渲染图 | ✅ `4c75889` |
| **D-2** | **光照输出线性 HDR（`R16G16B16A16_SFLOAT`）+ 独立 PresentPass（曝光/ACES/gamma/ImGui）+ BloomPass（亮部提取 + 4 级金字塔）+ G-Buffer Albedo 改 16F + 渲染图声明校验** | ✅ `adb622d` |
| D-2 补充 | 把「关掉 Bloom」这条最易漏验证层错误的分支做成命令行自检（`--selftest`） | ✅ `3bb7de9` |
| D-2 修复 | 修掉「关闭 SSAO 会让 `ambient` 乘上未定义内容」；自检扩展到 `--no-bloom` / `--no-ssao` 四种组合；顺手改掉 `mrt.frag` 里 Albedo=SRGB 时代的注释 | ✅ `3fed268` |
| **W1** | **运行时配置持久化**（`EngineConfig` + `config/engine.json` + 面板 Save/Reload）；顺带修掉**析构顺序导致的 VMA 泄漏**（这个 bug 一直被 TerminateProcess 掩盖着） | ✅ `25ae42f` |
| **W2** | **渲染图语义校验**：瞬时资源每帧必须有人写，否则 `endFrame` 直接抛异常。沿途逼出两个设计修正（Bloom 关闭改为强度置零、`importTexture` 常驻判定回写 desc） | ✅ `4986022` |
| **D-3** | **Bindless 材质描述符**（`descriptorIndexing`）：材质从"每材质一个 set"改成"一个 set 里 4 个纹理数组"，下标走 push constant。**`desc binds` 104 → 1**，FPS 452 → 498 | ✅ `bc703c0` |
| **D-4** | **多帧并行 + timeline semaphore**：2 帧在飞；每帧命令缓冲 / timeline 值 / UBO / G-Buffer / SSAO / HDR / Bloom / 描述符集全部隔离；`FrameInfo` 拆分 `frameInFlightIndex` / `swapchainImageIndex` / `frameNumber` | ✅ `cd7d695` |
| **D-5** | **Compute + 异步计算队列**：独立 compute queue / command pool / timeline；`ComputePipeline`；RenderGraph 的 storage buffer handle、buffer barrier、compute pass 与 buffer 语义校验；`ComputeProbePass` 完成 GPU 写 + CPU 读回验证 | ✅ `cd7d695` |
| **D-6（本地）** | **Job System**：`std::thread` worker pool、`submit()` / `JobHandle`、`parallelFor()`、异常传播、嵌套调用防死锁；`--jobtest` 与四种渲染 selftest 均通过 | 🟡 工作区未提交 |
| **D-7（本地）** | **EnTT ECS + Transform 层级 + 空间索引**：`entt::registry`、父子 Transform、world/normal matrix、均匀网格 broad phase、frustum-AABB culling；GeometrySystem 改为遍历可见实体 | 🟡 工作区未提交 |

**D-2 一共动了 25 个文件、+1039 行**，其中新增了 4 个 Pass 级文件（`BloomPass.*` / `PresentPass.*`）和 4 个 shader（`bloom_bright/down/up.frag` / `present.frag`）。
值得注意的是：**这 9 个新 Pass（1 亮部提取 + 3 降采样 + 3 上采样 + 1 Present + 1 PresentTransition）没有写一行屏障代码** —— 每个 Pass 只是声明 `read` / `writeColor`，同步全由渲染图推导。这是渲染图第一次被真正压力测试。

### W 阶段（插在 D-3 之前的两步，用户确认的顺序）

原本计划直接上 D-3（Bindless），但 D-2 的调参过程和那个 SSAO 的洞暴露了两个更该先解决的问题。两步都已完成：

| 步 | 解决的问题 | 关键产出 |
|----|-----------|---------|
| **W1** | "改参数必须重编译" —— D-2 调 Bloom 扫了四组值，每次都要完整重建 | `EngineConfig`（json 读写）+ 面板 Save/Reload + 退出自动保存 |
| **W2** | "同步正确 ≠ 语义正确" —— 渲染图保证屏障，但没人检查"下游读的瞬时图这帧有没有人写" | `RenderGraph::validateWriteCoverage()`，启动第一帧就报错并指名道姓 |

W1 还顺带挖出一个一直存在的真 bug：**析构顺序导致 Pass 持有的 VMA 分配在分配器销毁之后才释放**（详见坑 15）。它之所以从没暴露，是因为验证脚本一直用 `TerminateProcess` 杀进程，而 `TerminateProcess` 根本不跑析构函数 —— 也就是说**以前那句"验证层 0 字节"从来没有覆盖过退出路径**。这条教训比 bug 本身重要。

### 接下来（按顺序，不要跳）

> 主功能路线已拆分到 `FUTURE_PLAN.md`。当前阶段请先按 `TECH_DEBT.md` 处理技术债。

| 里程碑 | 内容 | 为什么排在这个位置 |
|--------|------|-------------------|
| ~~D-3~~ | ~~Bindless（`descriptorIndexing`）~~ | ✅ **已完成**（`bc703c0`）。`desc binds` 104 → 1，FPS 452 → 498。 |
| ~~D-4~~ | ~~多帧并行 + timeline semaphore~~ | ✅ **已完成（本地工作区，未提交）**：2 帧在飞；`--selftest` 四种组合各 120 帧 stderr 0；实测 566 FPS。 |
| ~~D-5~~ | ~~Compute + 异步计算队列~~ | ✅ **已完成（本地工作区，未提交）**：独立 compute queue + 双 timeline；RenderGraph 支持 storage buffer / compute pass / buffer barrier / buffer 语义校验；`ComputeProbePass` 验证 GPU 写 + CPU 读回。当前跨队列 producer/consumer 会显式拒绝。 |
| ~~D-6~~ | ~~作业系统（Job System）~~ | ✅ **已完成（本地工作区，未提交）**：worker pool + `JobHandle` + `parallelFor` + 嵌套防死锁 + 异常传播；`--jobtest` 通过。当前是基础设施，地形/体素尚未接入。 |
| ~~D-7~~ | ~~EnTT ECS + Transform 层级 + 空间索引~~ | ✅ **已完成（本地工作区，未提交）**：EnTT v3.16.0；parent/children + world transform propagation；均匀网格 + frustum culling；`Scene::selfTest()` 通过。 |

> D-3 完成意味着**"后面所有性能工作的前提"已经就位**：现在一次绑定可以画完整个场景。
> D-4 已完成：帧资源生命周期（命令缓冲、UBO、描述符集、渲染图瞬时图）都已按 frame slot 隔离。
> D-7 已完成。后续进入地形 / 体素主功能前，先处理 compute-graphics 跨队列依赖和 glTF 内部节点层级两项技术债。

### 已知技术债 / 代码与文档不一致的地方（**已验证，不是猜测**）

> 当前任务排序以 `TECH_DEBT.md` 为准；本节保留详细历史证据和旧编号。

1. **`mrt.frag` 的注释曾是 Albedo = SRGB 时代的**（第 62~64 行说"G-Buffer 的 albedo 附件同样是 SRGB 格式，硬件自动做 linear→sRGB 编码"）。
   D-2 已把 `GBufferFormat::Albedo` 改成 `R16G16B16A16_SFLOAT`，**注释现在是错的**：写入不再有自动编码，读出来就是线性值。同文件"向 4 个 G-Buffer 附件输出（0 是 Swapchain，这里不用）"也是动态渲染之前的话（已经没有 swapchain 附件了）。
   > ✅ **已修**（`3fed268`）：两处注释都改成了浮点格式的实际语义。
2. ~~**`SSAOPass` 关闭时，`ssao` 图的内容是未定义的。**~~
   > ✅ **已修**（`3fed268`）。原问题：`pass.setEnabled(frameInfo.settings.ssaoEnabled)` 会让整个 SSAO pass 不执行，但 `LightingPass` **无条件** `pass.read(resources.ssao)`，而 `ssao` 是渲染图 `createTexture` 出来的（`resetEachFrame = true`）→ 每帧布局从 `UNDEFINED` 转到 `SHADER_READ_ONLY`，内容不被保证；`lighting.frag` 里 `ambient *= ssao` 就会乘上垃圾值。而且 `ssao.frag` 自带的 `ubo.enabled < 0.5 → 输出 1.0` 分支**永远走不到**，因为 pass 被整体跳过了。
   >
   > 采用的就是下面"修法①"：开关下沉到着色器，Pass 永远执行，`enabled` 判断提到最前面（关闭时连 G-Buffer 采样都省掉）。修正前后同机位截屏逐像素对比 **100% 像素差为 0**，确认只在"关闭"路径上堵洞、不改变正常画面。
   >
   > 教训值得记：**这类缺陷验证层抓不到**。从 `UNDEFINED` 转出来在同步上完全合法，错的是"没人写、下游却当它有意义"这层语义。所以判据不是"验证层报没报"，而是"下游无条件读的每一张瞬时图，是不是每帧都一定有人写"。
   >
   > 与 Bloom 的情况**不完全同类**，这个区别值得记：`PresentPass` 也无条件 `read(bloomResult)`，但它在着色器里用 `bloomEnabled` 把采样整个跳过了 —— 那只是"多发了一层无意义的屏障"，不是语义缺陷。当时只给 Bloom 做了 `--selftest --no-bloom` 就以为覆盖住了，实际上真正有洞的是 SSAO。**教训：覆盖"某条分支"不等于覆盖"这类分支"。**
3. **`ReleaseMaterialSet()` 没有任何调用方。** 槽位回收 API 已经写好（`freeSlots` 栈 + `slotInUse`），但 `MaterialSystem::cleanup()` 只是 `materials.clear()`，从不归还槽位。也就是说"可回收"目前只是能力，没有使用者。
4. **`RenderGraph::setPassEnabled(name, enabled)` 实际不可用**（帧间 `passes` 是空的），见第 9 节。
5. ~~**`RenderGraph` 的 compute 路径只有骨架**~~ → **D-5 已补**：`addComputePass` 会记录到独立 compute command buffer；新增 `BufferHandle` / `readBuffer` / `writeBuffer` / buffer barrier，并把 buffer 纳入语义校验。仍需补跨队列 producer/consumer 调度。
6. **`Pass::writeDepth` 会静默改写清屏值**：`clear.depthStencil.depth == 0.0f && stencil == 0` 时被改成 `{1.0f, 0}`。想真的把深度清成 0 的调用方会被坑。
7. **`Swapchain` 里的深度资源已经没人用了**：构造函数仍然调 `createDepthResources()`（分配一张 depth image + view），但深度现在由 `GBuffer` 持有。同时 `swapChainFramebuffers` 永远是空的（`createFramebuffers(VkRenderPass)` 已无人调用），析构里那个循环是空转。`getFramebuffer()` / `getDepthImageView()` / `createFramebuffers()` 都是死接口。
8. **`IBLBaker` 里有死代码**：匿名命名空间的 `createOffscreenRenderPass()` 定义后从未调用；`makeFramebuffer()` 定义后从未调用；`pendingFramebuffers` / `cubeRenderPass` / `lutRenderPass` 都是 D-1(b) 迁移到动态渲染之后的残留（`createRenderPasses()` 现在是空函数，注释说明了"保留成空函数是为了不动 bake() 的调用顺序"）。
9. **`src/Core/Descriptor.h/.cpp` 不在渲染路径上**（`DescriptorAllocator` + `DescriptorBuilder`）。仍然被 `file(GLOB_RECURSE)` 编进目标，但没有任何渲染路径使用它。
   `adb622d` 已经在文件顶部加了「历史遗留 / 已不在渲染路径上，不要往这里加东西」的说明（理由是"将来做 D-4 多帧并行时，多池轮转会是有参考价值的另一种思路"）。**这是一处有意保留的遗留，不是漏删** —— 但其中提到的"还有零散引用"目前已经只剩它自己（全仓库除了 `Descriptor.h/.cpp` 没有别的翻译单元引用这两个类）。
10. **废弃 shader 仍在树里**：`shaders/light_cube.vert/.frag`、`shaders/shader.vert/.frag` 无人引用；`shaders/` 源目录里还躺着 `vert.spv` / `frag.spv` / `light_cube.*.spv` / `shader.*.spv` 等构建产物（真正的产物在 `build/shaders/`）。
11. **EnTT / spdlog 只配了 include 路径，一行代码都没用**（日志仍是 `std::cout`/`std::cerr`）。
12. **`gridPipeline` 不会被触发**：`GeometrySystem` 建了它、`grid_mrt.*` 也编了，但没有任何实体把 `RenderTagComponent::isGrid` 设成 `true`。
13. **`FrameInfo::frameIndex` 名字有误导性**：它装的是 `renderer->getCurrentImageIndex()`（交换链图像索引），不是帧计数器。
14. **`RenderTelemetry::vertices` 统计的是索引数**，不是顶点数（见第 1 节基线注）。
15. ~~**销毁顺序可疑（未实测）**~~ → ✅ **已修（W1，`25ae42f`）**，而且比当初估计的严重得多：
     不只是"表面先没了"，而是 `renderPipeline`（值成员，声明得比 `vulkanDevice` 晚）
     导致 Pass 持有的 VMA 分配在 `vmaDestroyAllocator` **之后**才释放 → 直接断言崩溃。
     现在 `~FirstApp` 把顺序全显式写出来。**原判断"当前 0 字节说明没被报出来"是错的** ——
     那是因为验证脚本用 `TerminateProcess`，根本没跑析构。详见坑 15。
16. **`Swapchain` 的 `imageCount` 与 ImGui 的 `MinImageCount` 不一致**：交换链取 `minImageCount + 1`，而 `ImGuiSystem` 里硬编码 `init_info.MinImageCount = 2`（ImGui 只在 `ImageCount < MinImageCount` 时报错，所以现在没触发）。
17. **"关闭 Bloom"仍有 7 个全屏 Pass 的固定开销（W2 引入，我自己不满意）**。
    为了让语义校验的不变量成立（每个被 read 的瞬时资源本帧一定有人写），
    Bloom 关闭时不再跳过 Pass，而是把亮部提取的强度置零 —— 输出全黑，视觉等价，
    但 Pass 照跑。代价是关掉 Bloom 后仍有 1 次半分辨率全屏 + 3 次降采样 + 3 次上采样。
    **更好的做法**是 PresentPass 准备两套描述符绑定（bloom 开 / 关各一套），
    关闭时直接不绑 bloomResult，这样 Pass 可以真正跳过。没做的原因是描述符要每帧重写、
    而当前是 init 时写一次；收益（省 7 个半分辨率 Pass）暂时不值得那份复杂度。
    **重新权衡的时机**：Bloom 变重时（加多级金字塔、改成 compute 版、或接入自动曝光用它的 mip 链）。
18. **`EngineConfig` 没有版本迁移，也没有多套预设**。`version` 字段写进去了但没人读；
    想加画质档位（低/中/高）需要扩展成目录 + 名字，现在只有一个 `engine.json`。
19. ~~**W2 的语义校验只管"图"，覆盖不到 buffer 和单个 mip / 层**。~~
    > ✅ **D-5 已扩展到 storage buffer**：瞬时 buffer 若在本帧被读但没人写，会像纹理一样抛错。
    > 单个 mip / 层的独立读写仍未细分，等真正出现分层资源时再补。
20. **验证脚本的两个隐含前提没有自动化保护**：必须"正常关窗退出"（否则覆盖不到析构路径，
    见坑 15）、必须"确认窗口到了前台"（否则截到桌面，数字看着正常但毫无意义）。
    两条都在脚本里做了显式检查并在失败时**丢弃结果**，但它们是外部脚本（在 `%TEMP%` 下），
    不在版本库里 —— 换机器要重新写一遍。
21. **bindless 的 `normalMaps` 数组绑了但没被采样（D-3）**。切线空间法线贴图仍然是
    "有意关闭"状态（`mrt.frag::getNormalFromMap` 保留待用），所以每个材质白占一个
    描述符 + 一次 `vkUpdateDescriptorSets`。256 个材质 = 256 个无用描述符（占总量 1/4）。
    **重新权衡的时机**：启用切线空间法线贴图时（要么让模型提供可靠切线，
    要么在 `mrt.frag` 里用偏导数构造 TBN 并处理 UV 接缝 —— 那个函数已经写好了）。
22. ~~**bindless 之后"运行期动态分配/复用材质槽位"有新的同步约束，目前没有防护（D-3）**。~~
    > ✅ **D-4 已修**：`releaseMaterialSet()` 在归还槽位前执行一次 `vkDeviceWaitIdle`，
    > 保证 GPU 不再读取被复用的数组项。当前没有运行期回收调用方，因此不构成热路径开销；
    > 若将来把材质流送放进每帧路径，应升级成基于 timeline semaphore 的延迟回收队列
    > 或 `UPDATE_AFTER_BIND` + 双缓冲。
23. **D-5 的异步 compute 尚不支持跨队列 producer/consumer**。compute 写、graphics 读，
    或 graphics 写、compute 读，目前会被 RenderGraph 显式拒绝（没有静默竞争）。
    真正的 GPU culling / 体素生成要把 indirect draw buffer 交给 graphics 消费时必须补：
    跨队列 ownership transfer（或 concurrent sharing）+ 双向 timeline semaphore 调度。
    这是 D-5 之后、接入 GPU 剔除/体素任务之前的第一优先级。
    另：当前 `ComputeProbePass` 是验收探针，本身有固定 GPU 开销；真正的 compute 任务
    接入后应删除或改为可选调试项。
24. **D-6 JobSystem 还是正确性优先的基础版**：单一全局任务队列；worker 内嵌套
    `parallelFor` 会退化为顺序执行；没有任务依赖图 / work stealing / 取消。
    地形和体素系统接入后，如果 chunk 粒度足够大、任务之间独立，这个版本可以继续用；
    但如果出现大量短任务或递归分块，需要升级任务调度器并重新做压力测试。
25. **D-7 的 glTF 内部节点层级仍未保留**：导入时几何已经烘焙到世界空间，
    entity 级 Transform 只能整体变换整个模型；要让 Sponza 内部节点/门/物件独立运动，
    需要保留 glTF node -> primitive instance 数据。
    另外 `SpatialIndex` 目前每帧重建，没有 dirty / 增量更新；地形 chunk 数量上来后
    应改成只在 transform/集合变化时重建，或做分块更新。

> 上面 1~3 是**会实际影响画面/正确性**的；4~12 是清理类；13~16 是命名/生命周期/一致性类；**17~25 是 W / D-3 / D-5 / D-6 / D-7 自己引入或暴露的**，其中 17、19、21、22、23、24、25 都有明确的"何时重新权衡 / 何时必须修"。

---

## 13. 换电脑 / 交接清单

1. 改 `CMakeLists.txt` 两处硬编码路径：`MY_VULKAN_DIR`（第 8 行）和 `GLSL_VALIDATOR`（第 85 行）。
2. 确认 `3rdParty/` 九个目录齐全（**注意 `find_package(glfw3 REQUIRED)` 这条外部依赖**）。
3. ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022"
   cmake --build build --config Debug
   ```
4. 跑一遍第 11 节的验证工作流（**包括第 3 步"关掉 Pass 的分支"**），确认基线数字对得上。
5. 读本文档 → 读 `ARCHITECTURE.md`（理解设计取舍）→ 读 `src/RHI/RenderGraph.h` 的头注释（理解同步模型）→ 从第 12 节"接下来"里挑 D-3 开始。
