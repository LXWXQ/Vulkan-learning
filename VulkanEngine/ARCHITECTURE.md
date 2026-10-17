# VulkanEngine "Sentinel" — 架构文档

> 新对话请优先读 `HANDOFF.md` 和 `TECH_DEBT.md`；未来主功能路线在 `FUTURE_PLAN.md`。
> 本文档描述已落地架构与历史薄弱点，当前技术债优先级以 `TECH_DEBT.md` 为准。

> 本文档回答「**为什么这样设计**」，以及「各模块的边界在哪」。
> 想快速上手构建 / 改代码，先读 `AI_Readme.md`；本文档假设你已经能把它跑起来。
>
> 描述对象：D-2 完成之后的状态（Vulkan 1.3 + 动态渲染 + 线性渲染图 + 延迟着色 + HDR 后效链）。

---

## 1. 技术选型表

| 维度 | 选型 | 为什么是这个（而不是别的） |
|------|------|--------------------------|
| 语言 | C++17 | 需要 `if constexpr` / 结构化绑定级别的东西不多，但 `inline static`（`Material` 的兜底纹理）和 `std::optional`（`QueueFamilyIndices`）省了很多样板。不上 C++20：MSVC 的 module / concept 生态对这个项目没有净收益。 |
| 图形 API | **Vulkan 1.3** | 选了 1.3 而不是 1.0，是因为要 `dynamicRendering` + `synchronization2` 这两个核心特性（见下）。目标平台只有 Windows 桌面 + 用户确认 GPU 支持到 1.4，所以**不为老 GPU 保留回退路径**：`Device::createLogicalDevice()` 直接查询特性，不支持就抛异常。 |
| 渲染通道 | **动态渲染**（`vkCmdBeginRendering`/`vkCmdEndRendering`） | 消灭 `VkRenderPass` + `VkFramebuffer`。收益：① 不用再为每个 Pass × 每个交换链图像建 Framebuffer；② 没有"Framebuffer 生命周期"这一类坑（录完就销毁 = 验证层刷屏）；③ 与渲染图天然契合 —— 一个 Pass 就是"一组 imageView + loadOp/clearValue"，正是 `VkRenderingInfo` 的形状。 |
| 同步 | **`synchronization2`**（`vkCmdPipelineBarrier2`） | `VkImageMemoryBarrier2` 把 stage 和 access 放在同一个结构里（旧接口要分 `srcStageMask`/`dstStageMask` 传参），渲染图生成屏障时不容易写错。另外 `VK_PIPELINE_STAGE_2_NONE` / `VK_ACCESS_2_NONE` 让"只转布局不关心访问"能自然表达。 |
| 渲染架构 | 延迟着色（Deferred + MRT） | 与"高度场地形 + 大量局部光源"的目标形态匹配：几何复杂度只影响 Geometry 一遍，光照开销与光源数相关而与三角形数无关。前向渲染在这个目标下要写一整套 clustered light list 才能打平。 |
| 帧图管理 | **自研线性渲染图**（`RenderGraph`） | 见第 6 节。核心判断：手写屏障 90% 的痛点在"顺序执行 + 记录上次访问状态"模型下就解决了，完整 DAG（重排 / 别名化 / 自动剔除）的复杂度不值得现在付。 |
| 显存分配 | VMA | 所有 image/buffer 都走 VMA，不用手写 `vkAllocateMemory` 的类型选择循环。`VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` 是默认；只有需要 CPU 写的 UBO/staging 才显式要 `HOST_ACCESS_SEQUENTIAL_WRITE`。 |
| 场景数据 | EnTT v3.16.0 ECS | `TransformComponent` / `HierarchyComponent` / `MeshComponent` / `MaterialComponent` / `RenderTagComponent` / `BoundsComponent`；D-7 起场景查询与渲染都走 `entt::registry`。 |
| 模型加载 | tinygltf（glTF 2.0）+ tinyobjloader（OBJ） | Sponza 是 glTF；OBJ 路径保留给调试模型（`sphere.obj` 当天空盒、`DamagedHelmet.obj` 当回退场景）。两条路径最终**都产出 `Model` + `SubMesh`**，渲染侧不区分来源。 |
| 纹理解码 | stb_image | LDR（`stbi_load` → RGBA8）与 HDR（`stbi_loadf` → RGBA32F）一个库全覆盖，不引入额外的 HDR 解码器。 |
| 数学 | GLM | 只有 `Camera` 手写了透视矩阵（因为 GLM 默认不是"Y 翻转 + 深度 0..1"的 Vulkan 约定），其余全用 GLM。 |
| 窗口 | GLFW | `GLFW_NO_API` + `glfwCreateWindowSurface`，不引入 GLFW 的图形上下文。 |
| 调试 UI | Dear ImGui 1.92.6（Vulkan + GLFW 后端） | 运行时可调（SSAO / 曝光 / Bloom / 相机），并显示遥测。**注意 1.92.6 的动态渲染字段位置**（见 AI_Readme 坑 3）。 |
| Shader 工具 | glslangValidator（GLSL 450 → SPIR-V） | CMake PRE_BUILD 阶段自动编译，源文件改动即重编，不需要手写构建脚本。`.comp` 也已经在 glob 里，D-5 的 compute shader 直接复用这条构建路径。 |
| 呈现模式 | `VK_PRESENT_MODE_IMMEDIATE_KHR` | 无垂直同步，跑分/延迟友好；代价是可能撕裂（开发阶段可接受）。 |
| 分辨率 | 硬编码 1920×1080、窗口不可缩放 | 缩放要牵扯"所有 Pass 的中间目标重建 + 所有管线重建 + 管线里的固定 viewport/scissor"（虽然 viewport 已是动态状态，但 `PipelineConfigInfo` 里的尺寸仍被烘焙进 viewport 初值）。在渲染图还不能自动重建资源之前，先不做。 |

---

## 2. 目录树

```
VulkanEngine/
├── CMakeLists.txt                  # 依赖、src glob、AssetPaths 生成、shader 编译
├── main.cpp                        # 入口
├── AI_Readme.md                    # 上手 / 构建 / 坑 / 待办
├── ARCHITECTURE.md                 # 本文档
├── cmake/
│   └── AssetPaths.h.in             # → build/generated/AssetPaths.h（绝对路径宏）
├── 3rdParty/                       # glfw glm imgui stb tinyobjloader tinygltf entt spdlog VMA
├── config/
│   ├── engine.json                 # 运行时配置（W1）：渲染参数 + 相机位姿，进版本库当默认预设
│   └── .gitignore                  # 忽略保存时的 *.tmp
├── Resources/
│   ├── Models/                     # DamagedHelmet/ Sponza/glTF/ + 若干 .obj
│   └── Texture/                    # environment.hdr 等
├── shaders/                        # GLSL 源码（+ 几个已废弃的 shader，见 AI_Readme 第 8 节）
├── RTR4_Notes/                     # 学习笔记，与引擎无关
└── src/
    ├── Core/
    │   ├── Application.h/.cpp        # FirstApp：构造顺序 + 帧循环
    │   ├── Descriptor.h/.cpp         # 【死代码】DescriptorAllocator/Builder
    │   ├── DescriptorManager.h/.cpp  # Set 0 全局 + Set 1 材质槽位
    │   ├── FrameInFlight.h          # kFramesInFlight（当前 2）
    │   ├── JobSystem.h/.cpp         # CPU worker pool / JobHandle / parallelFor
    │   ├── FrameInfo.h               # 每帧数据总线 + std140 静态断言
    │   ├── ImGuiSystem.h/.cpp        # 两段式 UI（prepare / render）
    │   ├── Material.h                # PBR 材质 + 兜底纹理
    │   ├── MaterialSlot.h            # kInvalidMaterialSlot
    │   └── MaterialSystem.h/.cpp     # 材质/纹理缓存
    ├── Render/
    │   ├── IRenderPass.h             # init / addToGraph / getName
    │   ├── RenderPipeline.h          # Pass 容器
    │   ├── RenderResources.h         # 帧内资源登记表（同步事实来源）
    │   ├── GeometryPass.h/.cpp       # G-Buffer 导入 + pass 声明
    │   ├── GeometrySystem.h/.cpp     # 3 条几何管线 + 逐子网格材质绑定
    │   ├── SSAOPass.h/.cpp           # 遮蔽图
    │   ├── LightingPass.h/.cpp       # 交换链导入 + HDR 目标 + pass 声明
    │   ├── LightingSystem.h/.cpp     # 光照全屏管线
    │   ├── BloomPass.h/.cpp          # 亮部提取 + 4 级金字塔
    │   ├── PresentPass.h/.cpp        # 曝光/ACES/gamma + ImGui 叠加
    │   └── IBLBaker.h/.cpp           # 一次性 IBL 烘焙
    ├── RHI/
    │   ├── CommandUtils.h
    │   ├── RenderGraph.h/.cpp        # 线性渲染图
    │   ├── Device.h/.cpp             # 实例/设备/队列/VMA/验证层
    │   ├── Swapchain.h/.cpp
    │   ├── Renderer.h/.cpp           # 帧生命周期（2 帧在飞 + graphics/compute 双 timeline）
    │   ├── GBuffer.h/.cpp            # 5 附件 + 格式常量
    │   ├── Pipeline.h/.cpp           # 图形管线（动态渲染）
    │   ├── Texture.h/.cpp
    │   └── CubemapTexture.h/.cpp
    └── Scene/
        ├── Scene.h/.cpp
        ├── Camera.h/.cpp
        ├── CameraController.h/.cpp
        ├── Components.h             # D-7 ECS components + AABB transform helper
        ├── SpatialIndex.h/.cpp      # uniform-grid broad phase
        ├── Model.h/.cpp
        └── GLTFLoader.h/.cpp
```

---

## 3. 渲染架构详解

### 3.1 三个层次

```
应用层        FirstApp          决定"这一帧要发生什么"，把 Pass 注册进渲染图，最后声明 present
   ↓
帧图层        RenderGraph       决定"屏障/布局/渲染作用域" —— 从 Pass 的声明自动推导
   ↓
RHI 层        Device/Swapchain/Renderer/GBuffer/Pipeline/Texture/CubemapTexture
                                决定"Vulkan 对象怎么建、怎么销毁"
```

三条边界是**刻意硬划**的：

- **Pass 里不允许出现一行同步代码**（`IRenderPass` 的接口里根本没有 `VkCommandBuffer`，只有 `addToGraph(RenderGraph&, FrameInfo&)`；命令缓冲只在 `Pass::execute` 的 lambda 里以参数形式出现，用于**绘制调用**）。
- **渲染图不知道 present，也不知道交换链**（`presentTransition` 由应用显式调用）。
- **渲染图不管采样器**（它只管 image/view/format/extent；采样器由使用方持有）。原因：采样器是"怎么读"的策略（NEAREST vs LINEAR、CLAMP vs REPEAT），和"谁写了这张图"无关，把它塞进图里只会让图的接口膨胀。

### 3.2 延迟着色 + MRT

Geometry 一遍写 4 个颜色附件 + 深度；之后所有屏幕空间效果都只读 G-Buffer，一次全屏三角形搞定。

**为什么用 MRT 而不是多个 Pass 各写一张**：一个 Pass 写 4 张图 = 一次光栅化、一次深度测试、4 次带宽写入；拆成 4 个 Pass = 4 次顶点处理 + 4 次深度测试（还要重新绑定管线/顶点缓冲）。几何通道是整个管线里唯一"按三角形数计费"的地方，必须只走一遍。

**代价**：G-Buffer 宽度是 `4 × 8B + 4B = 36 B/像素`（Position 16F×4 + Normal 16F×4 + Albedo 16F×4 + Pbr 8bit×4）。1920×1080 下 ≈ 75 MB 显存和相应的写带宽。Albedo 用 16F 是这个设计里最"奢侈"的一笔（见 3.4）。

**法线的量化**：`Normal` 用 `R16G16B16A16_SFLOAT` 存世界空间法线（`.w = 1`），而不是压缩成两个通道（octahedral encoding）+ 从深度重建位置。取舍：现在的做法写带宽高一倍，但 `lighting.frag` / `ssao.frag` 里不用做解码，也避免了重建位置时在深度边界上的精度问题。等 D-3/D-4 把带宽观测起来之后再考虑压缩。

### 3.3 天空盒是"走 G-Buffer 的普通几何"

`skybox_mrt.vert` 用 `gl_Position = pos.xyww` 把深度压到远平面（配合 `depthWriteEnable = FALSE` + `VK_COMPARE_OP_LESS_OR_EQUAL`），`skybox_mrt.frag` 往 **Albedo 附件**写线性 HDR 辐照度、往 **Normal 附件**写 `(0,0,0)`。

**为什么让天空盒走 GeometryPass 而不是单独一个 Pass**：
1. 不需要额外的深度/模板清理和额外的全屏绘制；
2. 天空的 HDR 值自动进入 G-Buffer，Lighting 只要看到"法线为 0"就原样透传 —— 天空的 tonemap 由 PresentPass 统一做，**不会双重色调映射**（D-2 之前天空盒自己在 shader 里做 ACES，而 Lighting 又做一次，结果天空被压暗发灰、边缘出现假光晕）。

**代价**：Albedo 附件必须是浮点（16F）。这是"天空盒借道 albedo 通道"这个设计直接导致的约束。

**"法线为 0"是个隐式契约**：`ssao.frag` 和 `lighting.frag` 都用 `length(N) < 0.1` 判断非几何。这是个**魔法值**，但它至少集中在一个地方（两处 shader 各一份注释）。真要改成显式标记，得往 Pbr 附件里腾一个通道。

### 3.4 线性 HDR 后效链（D-2 的核心）

```
LightingPass ──write──> hdrScene (R16G16B16A16_SFLOAT, 线性、未 clamp)
                            │
                    ┌───────┴────────┐
              BloomPass          PresentPass
         (在 HDR 上提亮部)    (读 hdr + bloom，做曝光/ACES/gamma)
                    │                │
                    └────> bloomResult ──> 交换链
```

**为什么必须把 tonemap 从光照通道里拿出来**：
Bloom 是"对亮部做模糊再叠加"的后效 —— 它必须作用在 tonemap **之前**的线性数据上。一旦提前 clamp 到 `[0,1]`（ACES 的最后一步就是 `clamp`），高光源信息就永久丢失，Bloom 只能把已经归一化的白色再涂白一遍，得到"整体发灰"而不是"光溢出"。

**为什么曝光也在 PresentPass 而不是光照通道**：曝光本质上是 tonemap 的**输入缩放**。放在光照通道里，Bloom 的阈值会随曝光一起变 —— 拖曝光滑块时辉光强度会跟着乱跳。放到 Present 之后，Bloom 的输入是"与显示无关的线性辐照度"，阈值才有稳定的物理含义（1.0 ≈ 刚好纯白）。

**为什么 PresentPass 还顺手画 ImGui**：UI 必须画在最终图像之上。放在 Present 的动态渲染作用域里，意味着以后往 Present 之前插任何后效（SSR / 色彩分级 / 暗角 / 锐化），UI 都自动不会被糊掉 —— 不需要每加一个后效就去改 ImGui 的绘制位置。

**D-2 顺带修掉的两个真问题**（都不是"记得改某一行"能避免的，是搬到 HDR 之后才暴露出来的）：

1. **天空盒被双重色调映射**。`skybox_mrt.frag` 原来在 G-Buffer 阶段就做了 ACES + gamma，理由是"天空盒会被 `lighting.frag` 通过法线为 0 的标记原样透传为最终像素"。D-2 之后光照输出线性 HDR、由 Present 统一 tonemap，于是天空被映射了两次：实测天空**压暗发灰**，而且被 clamp 到 1.0 的天空**整片超过 Bloom 阈值**，画面蒙上一层假雾（首次截图：过曝像素 7.1% → 0.3%、均值 +2，整张图的结构都变了）。
   修正后 `skybox_mrt.frag` 和普通几何一样，只往 HDR 图里写**线性值**。
2. **G-Buffer Albedo 必须是浮点**。为了让天空的 > 1.0 高光活到 Bloom，`GBufferFormat::Albedo` 从 `R8G8B8A8_SRGB` 改成 `R16G16B16A16_SFLOAT` —— 8bit 会在**写入时**就把高光截断成 1.0。（普通几何的 albedo 是 sRGB 贴图经硬件解码后的线性值，本来就落在 0~1，换格式后像素统计几乎无变化，已验证。）

**代价的量化**：D-2 的 FPS 从 547 掉到 457（−16%），全部来自新增的 7 个全屏 Pass。换来的是"后效链可以继续往下接" —— SSR / 自动曝光 / 色彩分级都是"往 Present 前面插一个 Pass"，而不是回头改 `lighting.frag`。

### 3.5 IBL：split-sum 近似

`IBLBaker::bake()` 在初始化阶段用**一条一次性命令缓冲**把等距柱状 HDR 变成三件套：

| 产物 | 尺寸 | 算法 | 为什么需要它 |
|------|------|------|-------------|
| `envCube` | 256² × 6 面，9 级 mip | 等距柱状 → 立方体（6 面分别渲染） | prefilter 需要 `textureLod`，必须有 mip 链 |
| `irradianceMap` | 32² × 6 面，1 级 mip | 半球余弦加权积分（`sampleDelta = 0.025`） | 漫反射 IBL：结果本身就=对天空做一次极粗糙的模糊 |
| `prefilteredMap` | 128² × 6 面，**5 级 mip** | GGX 重要性采样 + Hammersley + 按采样立体角估 mip | 镜面 IBL：每级 mip 对应一个粗糙度 |
| `brdfLut` | 256²，`R16G16_SFLOAT` | (NdotV, roughness) → (A,B)，1024 采样 | split-sum 的第二项，运行时零成本 |

**为什么是 split-sum 而不是"用环境图乘一个粗糙度系数"**：老实现是 `envSpecular *= (1.0 - roughness)` —— 能量完全不守恒，粗粗糙度时高光直接消失。split-sum 把镜面积分拆成"预过滤环境 × BRDF 积分表"，两项都可以离线算，运行时只有两次采样。实测效果：老 IBL 在曝光 1.0 下 **95.9% 像素过曝**，换成 split-sum 后降到 **7.1%**。

**为什么烘焙放 GPU**：irradiance 需要 `6×32×32` 个纹素 × 约 1.6 万次半球采样，prefilter 需要 `6×128×128×1.33` 个纹素 × 1024 次 GGX 采样 —— CPU 上要几十秒，GPU 上是一次性几十毫秒。

**烘焙实现的边界情况**：
- prefilter 每级 mip 的 `roughness = mip / (mipCount - 1)`，所以 5 级 mip → roughness ∈ {0, 0.25, 0.5, 0.75, 1.0}。`lighting.frag` 里的 `MAX_REFLECTION_LOD = 4.0` **与这个 5 是硬耦合的**，改 `kPrefilterMips` 必须同步改 shader。
- `envCube` 的 mip 链用 `vkCmdBlitImage` 逐面生成；每条 mip 之后都**逐 mip 精确转换布局**（而不是整图用 `GENERAL`）——注释里写明了原因：`GENERAL` 虽然功能正确，但验证层会对每次 blit 刷性能警告，把真错误淹掉。
- `irradiance.frag` 的采样数在 shader 里固定，所以 push constant 的 `sampleCount` 传 0；prefilter 才真的用它。

---

## 4. 每个 Pass 的输入 / 输出 / 职责

所有 Pass 都实现 `IRenderPass`：`init()` 建资源与管线，`addToGraph()` 声明本帧工作。
下表里的"资源"指 `RenderResources` 里登记的句柄。

### GeometryPass（`src/Render/GeometryPass.cpp` + `GeometrySystem`）

| | |
|---|---|
| **init** | 把 `GBuffer` 的 5 张附件**导入**渲染图（`initialLayout = UNDEFINED`，它们每帧被完整重写，内容不需要保留）；创建 `GeometrySystem`（3 条管线） |
| **声明** | `writeColor × 4`（Position/Normal/Albedo/Pbr，全部 `LOAD_OP_CLEAR` + 清成 `(0,0,0,0)`）+ `writeDepth`（`LOAD_OP_CLEAR`，清 1.0） |
| **execute** | `GeometrySystem::render(cmd, frameInfo)` |
| **输出** | G-Buffer 4 色 + 深度 |

`GeometrySystem::render` 的职责：
- 逐可见 EnTT entity（`MeshComponent` + `WorldTransformComponent`）读取已算好的 world / normal matrix；
- 按 `RenderTagComponent::isSkybox` / `isGrid` 选管线；
- 逐 `SubMesh` 决定材质槽位：**子网格自带就用它（glTF 路径），否则回退到 `obj.material->descriptorSetIndex` / `obj.materialSetIndex`（OBJ 路径）**；
- 用 `lastBoundSlot` 去重，**只在槽位真的变化时**才 `vkCmdBindDescriptorSets`；
- 没有有效槽位就 `std::cerr` 报错并跳过该子网格（而不是绑一个空 set 或崩掉）；
- 累加遥测：`drawCalls += 1`、`triangles += indexCount / 3`、`vertices += indexCount`。

### SSAOPass（`src/Render/SSAOPass.cpp`）

| | |
|---|---|
| **init** | `createTexture` 出 `R8_UNORM` 的 `ssao` 图；自建**输出采样器**（LINEAR + CLAMP_TO_EDGE，交给全局 Set 0 的 binding 6）；生成 64 个半球采样核（靠近中心更密集，`scale = mix(0.1, 1.0, t²)`）；自建 4×4 噪声贴图（RGBA32F，NEAREST + REPEAT，用一次性命令缓冲上传）；自建 `SSAOUbo`；自建局部 set（binding 0 = UBO，binding 1 = 噪声）；建 fullscreen 管线 |
| **声明** | `read(gbPosition)`、`read(gbNormal)`、`writeColor(ssao, CLEAR, 白 1.0)`；`setEnabled(settings.ssaoEnabled)` |
| **execute** | 绑定全局 set + 局部 set，`vkCmdDraw(3, 1, 0, 0)` |
| **输出** | `ssao`（R8_UNORM，1.0 = 无遮蔽；**天空/非几何直接输出 1.0**） |

**为什么自己 new 一个临时命令池**：噪声贴图上传需要一次性命令缓冲，这里刻意不复用渲染用的那个池（避免和帧录制耦合）。注释写明了这一点。

**为什么输出是单通道 R8 而不是 AO 的 RG**：本 Pass 只做遮蔽，不需要方向性遮蔽（GTAO 才需要）。输出 1.0 到三个通道是为了让可视化/调试时看起来正常。

**⚠️ 已知问题**：`setEnabled(ssaoEnabled)` 会让整 Pass 不执行，但 `LightingPass` 仍无条件读 `ssao`，而此时它是未定义的（详见 AI_Readme 第 12 节第 2 条）。

### LightingPass（`src/Render/LightingPass.cpp` + `LightingSystem`）

| | |
|---|---|
| **init** | 把**交换链的所有图像**导入渲染图（`initialLayout = UNDEFINED`，"present 之后布局回到未定义"是规范保证的）；`createTexture` 出 `hdrScene`（`kHdrSceneFormat` = `R16G16B16A16_SFLOAT`，`COLOR_ATTACHMENT | SAMPLED`）；建 `LightingSystem` |
| **声明** | `read × 9`：gbPosition / gbNormal / gbAlbedo / gbPbr / ssao / environment / irradiance / prefiltered / brdfLut；`writeColor(hdrScene, CLEAR, (0.01, 0.01, 0.01, 1))` |
| **execute** | 绑定全局 set 0，`LightingSystem::render(cmd, frameInfo)`（一次全屏三角形） |
| **输出** | `hdrScene`：**线性、未 clamp** 的辐照度 |

**职责划分里一个不对称的地方**：`LightingPass` 还承担了"导入交换链图像"的职责。这是历史原因（最初它就是写交换链的那个 Pass），代价是 **`LightingPass::init()` 必须在 `PresentPass::init()` 之前跑**（Present 要读 `resources.swapchainImages[frameIndex]` 和 `hdrScene` 的 view）。依赖关系靠 `Application` 里 `addPass` 的顺序保证，**没有编译期或运行期检查**。要清理的话，把交换链导入挪到 `Application` 里更合适。

**清屏色为什么是 0.01 而不是 0**：纯黑背景下 Bloom 的软阈值会有轻微的非零输出（人眼对绝对黑的邻域很敏感），给一点点底噪可以避免"暗部出现一圈脏晕"。这是调参结果，不是物理量。

### BloomPass（`src/Render/BloomPass.h/.cpp`，D-2 新增）

| | |
|---|---|
| **init** | 由 `hdrScene` 的 extent 逐级减半创建 `kLevelCount = 4` 张 `bloomLevels`（格式跟 `hdrScene`，`COLOR_ATTACHMENT | SAMPLED`）；`bloomResult = bloomLevels[0]`；一次性分配 7 个描述符 set 并写好 view（`1 + 2*(kLevelCount-1)`）；建 3 条管线（bright / down / up，共用 `fullscreen.vert`） |
| **声明** | `Bloom.Bright`：`read(hdrScene)` + `writeColor(bloomLevels[0], CLEAR)`<br>`Bloom.Down{i}`：`read(bloomLevels[i])` + `writeColor(bloomLevels[i+1], DONT_CARE)`<br>`Bloom.Up{i}`（i 由粗到细）：`read(bloomLevels[i+1])` + `writeColor(bloomLevels[i], LOAD)`<br>每个 pass 都 `setEnabled(settings.bloomEnabled)` |
| **输出** | `bloomLevels[0]`（= `bloomResult`），960×540 |

**结构**：
```
hdrScene ──bright──> L0 ──down──> L1 ──down──> L2 ──down──> L3
                      ^           ^           ^           ^
                      └────up─────┴─────up────┴─────up────┘
```
尺寸：`960×540 → 480×270 → 240×135 → 120×67`（每级 `max(1, e/2)`）。

**为什么用金字塔而不是"一次大半径高斯模糊"**：同等辉光半径下，金字塔的采样数是 O(N)（每级 13 + 9 个抽样），单级大核高斯是 O(r²)。更重要的是金字塔**天然给出多尺度的光晕** —— 近处的锐利辉光来自细级、远处的大片光雾来自粗级，这正是真实镜头/眼睛的散射形态。

**为什么上采样用"加法混合 + `LOAD_OP_LOAD`"**：等于把粗级的贡献累加回细级，**不需要额外的累加缓冲**。代价是同一个 Pass 里对 `bloomLevels[i]` 既当颜色附件读（LOAD）又写的语义（见 3.2 的 dedup 说明），以及必须把 `bloomLevels[i]` 声明为"既能被采样又能被渲染"（usage 两位都给）。

**为什么上采样管线要单独一条**（不能复用一个"通用后效管线"）：混合状态是**烘焙进管线**的（`srcColorBlendFactor = ONE`, `dstColorBlendFactor = ONE`），不是动态状态。所以 bright / down 用无混合的配置，up 用加法的配置，必须是两条（这里是三条，因为 push constant 大小也不同）。

**描述符 set 只在 init 写一次**：因为 bloom 的每一级都由渲染图持有、`VkImageView` 运行期不重建，所以不需要每帧 `vkUpdateDescriptorSets`。**代价**：级数 / 分辨率改变时必须重建 set（当前"窗口不可缩放"正好掩盖了这一点）。

### PresentPass（`src/Render/PresentPass.h/.cpp`，D-2 新增）

| | |
|---|---|
| **init** | 自建 set layout（binding 0 = `hdrScene`，binding 1 = `bloomResult`）+ 一个 LINEAR/CLAMP 采样器；分配 1 个 set 并把两个 view 写进去；把采样器存进 `resources.hdrSampler`（留给以后接自动曝光）；建管线（输出格式 = **交换链实际格式**） |
| **声明** | `read(hdrScene)`、`read(bloomResult)`、`writeColor(swapchainImages[frameIndex], DONT_CARE)` |
| **execute** | 全屏三角形（push constant：`exposure` / `bloomIntensity` / `bloomEnabled`）→ 然后 `imguiSystem->renderDrawData(cmd)` |
| **输出** | 交换链图像 |

**`loadOp = DONT_CARE`**：整屏都会被全屏三角形覆盖，读回旧内容是纯粹的带宽浪费。

**`bloomEnabled` 走 push constant 而不是重建管线**：关 Bloom 时"重建一条无 Bloom 的管线"要多维护一份 pipeline 状态；用一个 float 开关 + `if` 分支的代价可以忽略（而且是 uniform 分支，不会有 warp divergence）。

**⚠️ 依赖 `bloomResult` 总是合法**：即使 `bloomEnabled == false`（Bloom 的 pass 全被禁用、`bloomResult` 本帧没被写），描述符 set 里仍然是那张合法的 image view —— shader 里 `if (bloomEnabled > 0.5)` 保证不会采它。注释里明确写了这个设计意图。**但**渲染图仍会为它发一个 `UNDEFINED → SHADER_READ_ONLY` 的屏障（内容被"丢弃"，无害但多余）。

### PresentTransition（`RenderGraph::presentTransition`）

不是 `IRenderPass`，是应用显式声明的一个"只有布局转换、没有附件"的 pass。
`endFrame()` 看到 `colors.empty() && depths.empty()` → 只发屏障，**不** `vkCmdBeginRendering`。转换到 `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`，之后 `Renderer::endFrame()` 里的 `vkQueuePresentKHR` 才能用这张图。

---

## 5. 资源生命周期

### 5.1 谁创建 / 谁销毁

| 资源 | 创建者 | 归属 | 销毁者 |
|------|--------|------|--------|
| G-Buffer 4 色 + 深度 | `GBuffer` | `GBuffer`（`FirstApp::gBuffer`） | `~GBuffer()`（`vmaDestroyImage` + `vkDestroyImageView`） |
| G-Buffer 采样器 | `GBuffer` | 同上 | `~GBuffer()` |
| 交换链 + ImageView + **depth 资源** | `Swapchain` | `Swapchain` | `~Swapchain()`（depth 已无人使用，但仍在分配/销毁） |
| 交换链 `VkFramebuffer` | —（`createFramebuffers` 已无人调用） | — | 析构里的循环空转 |
| 全局 UBO | `FirstApp::createUniformBuffers()` | `FirstApp`（裸 `VkBuffer` + `VmaAllocation`） | `FirstApp::~FirstApp()` 手动 `vmaDestroyBuffer` |
| 环境 HDR 纹理 | `Texture` | `FirstApp::environmentTex`（`unique_ptr`） | `~Texture()` |
| IBL 三件套（2 个 cubemap + 1 个 LUT image） | `IBLBaker::createTargets()` | `IBLBaker` | `~IBLBaker()` |
| IBL 的 4 条管线 / 2 个 pipeline layout / 描述符池 | `IBLBaker` | `IBLBaker` | `~IBLBaker()` |
| `ssao` 图 | `RenderGraph::createTexture`（在 `SSAOPass::init` 里调） | **渲染图**（`owned = true`） | `~RenderGraph()` |
| `hdrScene` 图 | `RenderGraph::createTexture`（在 `LightingPass::init` 里调） | 渲染图 | `~RenderGraph()` |
| `bloomLevels[0..3]` | `RenderGraph::createTexture`（在 `BloomPass::init` 里调） | 渲染图 | `~RenderGraph()` |
| SSAO 输出采样器 / 噪声贴图+采样器 / SSAO UBO | `SSAOPass` | `SSAOPass` | `~SSAOPass()` |
| Bloom 采样器 / set layout | `BloomPass` | `BloomPass` | `~BloomPass()` |
| Present 采样器 / set layout | `PresentPass` | `PresentPass` | `~PresentPass()` |
| 所有 `VkPipeline` / `VkShaderModule` | `Pipeline` | `Pipeline`（Pass 里是 `unique_ptr`） | `~Pipeline()` |
| 所有 `VkPipelineLayout` | 各 Pass / System | 各自 | 各自析构 |
| 所有 `VkDescriptorSet` | `DescriptorManager`（池） | 描述符池 | `DescriptorManager::cleanup()` 销毁池（set 随之失效，所以只清索引表） |
| 材质/纹理对象 | `MaterialSystem`（`shared_ptr` 缓存） | `MaterialSystem` | `MaterialSystem::cleanup()` |

### 5.2 导入 vs 图自有（`importTexture` / `createTexture`）

这是渲染图里**最重要的所有权规则**：

```cpp
// 图自有：渲染图创建 VMA image + view，owned = true，析构时销毁
RenderGraph::TextureHandle createTexture(const TextureDesc& desc);

// 导入：调用方提供 image/view，owned = false，渲染图只记录描述
RenderGraph::TextureHandle importTexture(const TextureDesc&, VkImage, VkImageView,
                                         VkImageLayout initialLayout);
```

- **`createTexture` 用于"这一帧的存在只是为了给别的 Pass 当前置"的中间目标**：`ssao`、`hdrScene`、`bloomLevels`。它们没有外部语义，随时可以被替换成别的实现（比如把 Bloom 换成 compute 版本时，`bloomLevels` 依然由图创建）。
- **`importTexture` 用于"生命周期由别人决定"的资源**：GBuffer 附件（属于 `GBuffer`）、交换链图像（属于 `Swapchain`）、环境图（属于 `Texture`）、IBL 三件套（属于 `IBLBaker`）。渲染图**绝不能**销毁它们。

### 5.3 `resetEachFrame`：跨帧常驻资源的特殊处理

```cpp
e.resetEachFrame = (initialLayout == VK_IMAGE_LAYOUT_UNDEFINED);
```

`beginFrame()` 里：

```cpp
for (TextureEntry& e : textures) {
    if (!e.resetEachFrame) continue;
    e.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    e.stage  = VK_PIPELINE_STAGE_2_NONE;
    e.access = VK_ACCESS_2_NONE;
}
```

**为什么需要这个区分**：对于每帧被完整重写的瞬时资源（G-Buffer、HDR 图、Bloom 各级），"上一帧结束时它是什么布局"对下一帧没意义 —— present 之后交换链布局回到未定义，G-Buffer 反正会被 clear。把它们复位成 `UNDEFINED` 可以让下一帧的屏障带上 `oldLayout = UNDEFINED`（= 允许驱动丢弃内容，可能省一次 flush）。

但对**跨帧常驻的只读资源**（IBL cubemap、BRDF LUT、环境图），复位就是**错的**：它们的内容是上一帧（其实是初始化时）留下的，`oldLayout = UNDEFINED` 在语义上等于"我不知道里面是什么"，驱动可以合法地丢弃 → IBL 变黑或闪烁。

**这个区分的表达方式是一个约定**（"`initialLayout` 不是 UNDEFINED 就说明它是常驻的"），不是显式的参数。想改的话应该加一个 `bool persistent` 参数 —— 现在这个隐性约定是能工作的，但它靠的是"IBL 资源导入时给了 `SHADER_READ_ONLY`"这个事实。

### 5.4 销毁顺序

`FirstApp` 的成员析构顺序是声明的**逆序**：

```
renderResources → renderGraph → renderPipeline → iblBaker → imguiSystem
→ scene → descriptorManager → gBuffer → renderer → vulkanSwapchain
→ vulkanDevice → window
```

几个值得注意的点：

- `renderGraph` 在 `renderPipeline` **之前**销毁。当前安全（Pass 的析构只碰自己的管线/采样器，不碰渲染图），但这是个脆弱顺序：任何 Pass 如果在析构里访问 `renderGraph.getView(...)` 就会炸。
- `descriptorManager` 在 `renderPipeline` **之后**销毁 —— 这是**必需**的：Pass 持有的 `VkDescriptorSet` 来自 `DescriptorManager` 的池，池必须先活到 Pass 析构完（虽然 Pass 析构并不释放 set，但保持这个顺序更安全）。
- **`FirstApp::~FirstApp()` 的函数体里先 `glfwDestroyWindow()` + `glfwTerminate()`，然后成员才开始析构**。也就是说 `~Device()` 里的 `vkDestroySurfaceKHR` 与 `~ImGuiSystem()` 里的 `ImGui_ImplGlfw_Shutdown()` 都发生在窗口已被销毁之后。当前验证层没报出来（见 AI_Readme 第 12 节第 15 条），但这是"能不能不出错取决于驱动/loader"的写法，应该把 `glfwTerminate()` 移到最后。

---

## 6. 同步模型

### 6.1 声明驱动

```cpp
auto& p = graph.addGraphicsPass("Lighting");
p.read (resources.gbPosition);                                        // → SHADER_READ_ONLY_OPTIMAL
p.writeColor(resources.hdrScene, VK_ATTACHMENT_LOAD_OP_CLEAR, clear); // → COLOR_ATTACHMENT_OPTIMAL
p.execute([...](VkCommandBuffer cmd) { /* 绘制 */ });
```

`Pass::read` / `writeColor` / `writeDepth` / `writeStorage` / `readStorage` / `transitionTo` 每一个都往 `pass.accesses` 里 push 一条：

```cpp
struct Access { TextureHandle texture; VkImageLayout layout;
                VkPipelineStageFlags2 stage; VkAccessFlags2 access; };
```

访问类型到三元组的映射是**固定的**（这是渲染图能自动工作的基础）：

| 声明 | layout | stage | access |
|------|--------|-------|--------|
| `read` | `SHADER_READ_ONLY_OPTIMAL` | `FRAGMENT_SHADER`（默认） | `SHADER_SAMPLED_READ` |
| `readStorage` / `writeStorage` | `GENERAL` | `COMPUTE_SHADER` | `SHADER_STORAGE_READ` / `..._WRITE` |
| `writeColor` | `COLOR_ATTACHMENT_OPTIMAL` | `COLOR_ATTACHMENT_OUTPUT` | `COLOR_ATTACHMENT_WRITE` |
| `writeDepth` | `DEPTH_ATTACHMENT_OPTIMAL` | `EARLY_FRAGMENT_TESTS \| LATE_FRAGMENT_TESTS` | `DEPTH_STENCIL_ATTACHMENT_WRITE` |
| `transitionTo` | 调用方指定 | 调用方指定（默认 `NONE`） | 调用方指定（默认 `NONE`） |

> 注意 `read` 的 stage 默认写死 `FRAGMENT_SHADER`。如果将来有 Pass 在**顶点着色器**里采样 G-Buffer，必须显式传 stage，否则屏障的 `dstStageMask` 会漏掉顶点阶段 → 罕见的"偶发花屏"。

### 6.2 屏障推导

每张图记住"上一次被谁以什么状态访问过"，下一次访问时插一条最小必要的屏障：

```cpp
const bool isRead = (acc.access & kWriteBits) == 0;
const bool sameState = (e.layout == acc.layout && e.stage == acc.stage && e.access == acc.access);
if (sameState && isRead) return;    // 连续两次完全相同的只读访问 → 不需要屏障
// 其余（含 write→write、以及任何状态变化）一律发屏障 —— 偏保守，但保证正确
VkImageMemoryBarrier2 b{};
b.srcStageMask = e.stage;   b.srcAccessMask = e.access;    // ← 来自"上一次访问"
b.dstStageMask = acc.stage; b.dstAccessMask = acc.access;  // ← 来自"这一次声明"
b.oldLayout = e.layout;     b.newLayout = acc.layout;
b.image = e.image;
b.subresourceRange = { aspectOf(e.isDepth), 0, e.desc.mipLevels, 0, e.desc.arrayLayers };
vkCmdPipelineBarrier2(cmd, &dep);
```

`kWriteBits = SHADER_WRITE | COLOR_ATTACHMENT_WRITE | DEPTH_STENCIL_ATTACHMENT_WRITE | TRANSFER_WRITE | HOST_WRITE`。

**三条附加规则**（D-2 后效链变长时补上的硬化）：

1. **同一 Pass 内对同一张纹理的重复访问只保留最后一次声明**（`endFrame` 的第 0 步）。
   Bloom 的上采样就是典型：它既要把粗一级当纹理采样，又要把细一级当颜色附件累加写入 —— 如果按声明顺序各插一条屏障，会变成"转到 `SHADER_READ`、再转回 `COLOR_ATTACHMENT`"，白白多一次转换。屏障本来只需要保证"**进入这个 Pass 时**资源处于正确布局"，所以中间那次声明可以直接丢掉。
   > 注意：这条规则**不会**把 `writeColor` / `read` 的语义搞混 —— 被保留的是最后一条声明的 `layout/stage/access`，而附件列表（`pass.colors` / `pass.depths`）是独立的、不受影响的。
2. **空的、没有任何依赖项的屏障会被静默跳过**：如果本次访问不需要换布局，而且**前后两侧的 stage 和 access 全是 `NONE`**，这条屏障在 Vulkan 里没有任何意义（sync1 要求 src 或 dst 至少一项非 `NONE`），留着只会在特定 Pass 顺序下变成验证层报错。
3. **`write → write`（同一布局）仍然会发屏障** —— 偏保守，但保证正确。

**为什么不做"跨 Pass 合并相邻屏障"**：一个 Pass 里对同一张图的多次访问已经按规则 1 处理了；跨 Pass 的合并需要知道后续访问，而线性模型的语义就是"逐 Pass 结算"。等 D-4（多帧并行）真的需要减少 barrier 数量时再考虑。

**保守策略的代价**：Bloom 的上采样链上，`bloomLevels[i]` 每帧会被"down 写 → up 读改写"各来一遍，两边的布局都是 `COLOR_ATTACHMENT_OPTIMAL`（up 用 `LOAD_OP_LOAD`），所以每条 up 都会带一条 `write → write` 屏障。7 个 Bloom Pass 加起来是几条多余的屏障 —— 在 460 FPS 的量级上完全不值得为此引入风险。

### 6.3 声明合法性校验：把静默错误提前到录制前（D-2 新增）

"声明即同步"的**代价**是漏声明不会报错 —— 只会出屏障缺失或 usage 不匹配的怪画面。所以在 `emitBarrierIfNeeded` 之前，`endFrame` 会先做一轮静态检查，把最常见的几类错误变成**带纹理名和 Pass 名的异常**：

| 检查 | 触发条件 |
|------|---------|
| 句柄越界 | `acc.texture.id >= textures.size()` |
| 颜色附件 usage | 被声明为颜色附件，但创建/导入时 `usage` 没有 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` |
| 深度附件 usage | 同上，缺 `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` |
| 采样源 usage | 访问位含 `SHADER_SAMPLED_READ`，但缺 `VK_IMAGE_USAGE_SAMPLED_BIT` |
| 存储图像 usage | 访问位含 `SHADER_STORAGE_READ/WRITE`，但缺 `VK_IMAGE_USAGE_STORAGE_BIT` |

**为什么这些检查值得单独写**：不加的话，失败路径是"验证层在某帧报一句 layout/usage 不符 + 画面花掉"，而**要等到那一帧真的被录制才会炸**（可能在几十秒之后、也可能只在某个 Pass 被关掉时才炸）。加上之后，错误在第一帧就带着名字抛出来。

**它为什么能成立**：因为渲染图**知道**每张图的 `usage`（`createTexture` / `importTexture` 时记下来的 `TextureDesc`）。这是"资源描述必须经过渲染图"这个设计约束换来的能力 —— 也是为什么给新 Pass 加资源时，`usage` 参数不能随手填（填错了要么被这里拦下，要么就是验证层的事）。

### 6.4 「漏声明就等于漏同步」

这是使用渲染图**唯一**的心智负担：

- 你在 `execute` 里 `texture(someTexture, uv)`，但**没有** `p.read(someTexture)` → 渲染图不知道这次访问 → 不会插 `SHADER_READ_ONLY` 屏障 → 布局可能还是 `COLOR_ATTACHMENT_OPTIMAL`（**验证层会报 layout mismatch**，如果没开验证层就是花屏或驱动 hang）。
- 你在 shader 里写了某张图，但**没有** `p.writeColor(...)` → 下一帧那张图的 `layout` 记录停留在旧值 → 屏障的 `oldLayout` 是错的。

排查手法：**任何"画面在某些帧才错"或"验证层报 layout 不符"的问题，第一件事是核对 `addToGraph` 里的声明清单和 shader 里的实际采样/写入是否一一对应**。
好消息是验证层几乎总能抓到这类错误（`VUID-vkCmdDraw-None-...` / `SYNC-HAZARD-READ-AFTER-WRITE`），所以"stderr 0 字节"这条验收标准在这里价值最高。

**还有一类"声明齐全但仍然错"的情况**：Pass 被 `setEnabled(false)` 跳过，于是它的输出图整帧没人写、布局停在 `UNDEFINED`，但后面照样有 Pass 声明要采样它。这类问题验证层**不报**（从规范角度它是合法的：从 `UNDEFINED` 转出来就是"内容未定义"）。

### 6.4b 语义校验：把"没人写"变成异常（W2 新增）

上一节说的是**同步**正确性。这一节是**语义**正确性 —— 两者是正交的，而验证层只覆盖前者。

`RenderGraph::validateWriteCoverage()` 在 `endFrame()` 里、**录制任何命令之前**执行，按声明顺序遍历所有**已启用**的 Pass，维护"已被写过"集合，遇到"读了没人写"就抛异常。

它为什么只需要顺序遍历、不需要真正的 DAG 分析：因为这是**线性**渲染图（决策 2），Pass 严格按声明顺序执行，顺序扫描得出的结论就是准确的。

同一 Pass 内既采样又写同一张图是允许的（Bloom 上采样），所以实现里**先登记本 Pass 的写、再看本 Pass 的读**。常驻资源（`desc.persistent`，即 IBL 烘焙结果那一类）豁免。

报错会指出**本该由谁写**，包括"那个 Pass 这一帧被跳过了"：

```
Pass "Lighting" 读取了瞬时纹理 "SSAO"，但这一帧到目前为止没有任何 Pass 写过它。
  它本来是由 Pass "SSAO（本帧被 setEnabled(false) 跳过）" 写的。
```

**这条校验逼出了两个设计修正，都不是"为了让校验通过"而妥协：**

1. **Bloom 关闭改为"强度置零"，不再跳过 Pass。** 试过"关闭时不声明对该图的读"，
   结果验证层刷屏：`expects layout SHADER_READ_ONLY_OPTIMAL, current layout UNDEFINED`。
   原因是它**绑在 PresentPass 的描述符里** —— 只要绑着，这个布局转换就省不掉；
   没人读 ⇒ 渲染图不发屏障 ⇒ 图像停在 `UNDEFINED`。
   所以选"关闭 = 强度 0"：Pass 照跑、输出全黑、视觉等价于关闭，而两张图每帧都被写过、
   布局永远正确。**代价**是关掉 Bloom 仍要跑 7 个全屏 Pass（都在半分辨率以下）。
2. **`importTexture` 的常驻判定回写进 `desc.persistent`。** 第一版校验把 `Environment` / IBL
   误判成"没人写的瞬时资源" —— 因为 `resetEachFrame` 来自参数、校验读的是 `desc.persistent`，
   两个字段各说各话。同一事实有两个来源就是 bug 温床，直接合并成一个。

**它的真正价值**：把一条本来靠人记的规则变成机器守的规则。加新 Pass 时不用再推理
"我关掉它会不会让下游读到垃圾" —— 关掉就会报错。


### 6.5 `presentTransition` 为什么必须显式声明

```cpp
// RenderGraph::presentTransition
Pass& p = addGraphicsPass("PresentTransition");
p.transitionTo(handle, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
```

三个理由：

1. **渲染图不应该知道交换链**。交换链是应用层的概念（`Swapchain` 拥有它、`Renderer` 按 `imageIndex` 选用），把它塞进渲染图会让图从"通用帧图"退化成"这一款应用的帧图"。
2. **时机必须由应用控制**。转换必须在**本帧最后一个 Pass 之后**（不能插在中间），而"哪个是最后一个"只有应用知道。
3. **它是同步的一部分，不能隐式**。如果渲染图"猜"哪张图要被 present（比如"最后被写的颜色附件"），那在离屏渲染 / 多窗口 / 截图这些场景下就会猜错，而且错得很隐蔽。

跨帧的职责边界也因此很清楚：
- **帧内同步**（image 布局、stage/access 屏障）→ 渲染图
- **帧间同步**（fence、semaphore、交换链图像可用性）→ `Renderer`

### 6.6 帧间同步（D-4）

`Renderer` 现在是 **2 帧在飞**：

```
2 个 VkCommandBuffer                    // 每个 frame slot 一条
1 个 timeline VkSemaphore               // frame slot 完成值
2 个 imageAvailable VkSemaphore         // 每个 frame slot 一枚
N 个 renderFinished VkSemaphore         // 每个交换链图像一枚
```

`beginFrame()`：
1. 用 `vkWaitSemaphores(timeline, submissionValues[slot])` 等这个 slot 上一轮完成；
2. `vkAcquireNextImageKHR(imageAvailable[slot])`；
3. reset / begin 该 slot 的命令缓冲。

`endFrame()`：
1. 提交时 wait `imageAvailable[slot]`；
2. signal `renderFinished[imageIndex]` 与 timeline 的本帧数值；
3. `vkQueuePresentKHR` 只等 `renderFinished[imageIndex]`。

**为什么瞬时资源也必须每帧一份**：GPU 执行第 N 帧时 CPU 已经在录制第 N+1 帧；G-Buffer、SSAO、HDR、Bloom 金字塔若共用，会让第 N+1 帧覆盖第 N 帧仍被采样的内容。这个 hazard 验证层不一定能完整报出来，所以 D-4 把它们全部按 frame slot 复制。

**必须保留的细节**：`pWaitDstStageMask` 仍用 `COLOR_ATTACHMENT_OUTPUT`，让 acquire 等待只卡颜色输出阶段；改成 `TOP_OF_PIPE` 会损失顶点/传输阶段的并行。
### 6.7 异步 Compute（D-5）

- 设备优先选择独立 compute queue family，没有独立队列时回退 graphics family。
- `Renderer` 每帧同时录制 graphics command buffer 与 compute command buffer；
  两条队列各自信号一枚 timeline semaphore，CPU 复用 frame slot 前同时等待两者。
- `RenderGraph` 新增 `BufferHandle`、`createBuffer/importBuffer`、`readBuffer/writeBuffer`。
  Buffer hazard 由 `VkBufferMemoryBarrier2` 推导，buffer 也纳入语义校验。
- `ComputePipeline` 只创建 `VK_PIPELINE_BIND_POINT_COMPUTE` 管线。
- `ComputeProbePass` 是验收探针：GPU 生成 1024 个 uint，再做 checksum 到 host-visible
  buffer，下一轮 CPU 读回验证，证明 compute 提交、buffer barrier、host readback 都工作。

> 当前限制：compute 与 graphics 之间如果存在 producer/consumer 资源依赖，RenderGraph 会抛异常。
> 真正的 GPU culling / 体素生成接入时，需要补跨队列 ownership transfer 与 semaphore 调度。

### 6.8 CPU Job System（D-6）

- `JobSystem` 用 `std::thread` 创建 `hardware_concurrency - 1` 个 worker，主线程保留给提交/等待。
- `submit()` 返回 `JobHandle`；`JobHandle::wait()` 等待完成并传播任务异常。
- `parallelFor(begin,end,grain,fn)` 按 grain 切 chunk，提交后等待全部完成。
- worker 内再次调用 `parallelFor` 时退化为顺序执行，避免所有 worker 同时等待队列任务造成死锁。
- `--jobtest` 是独立无 Vulkan 自检；`--selftest` 启动时也会跑一遍 JobSystem 自检。

> 当前版本优先正确性：全局队列、无 work stealing、无任务依赖图、无取消。
> terrain / voxel 接入后若出现大量短任务或递归分块，再根据 profile 升级调度器。

### 6.9 ECS / Transform 层级 / 空间索引（D-7）

- 场景实体由 `entt::registry` 管理，渲染与场景逻辑通过 component view 查询。
- `HierarchyComponent` 保存 parent + children；`Scene::setParent()` 拒绝自环/循环。
- `Scene::updateWorldTransforms()` 递归计算 `world = parentWorld * local`，并生成 normal matrix。
- `BoundsComponent` 保存模型局部 AABB；`WorldBoundsComponent` 保存变换后的世界 AABB。
- `SpatialIndex` 是均匀网格：世界 AABB 插入网格，frustum 先做 broad-phase AABB 查询，
  再做精确 frustum-AABB 测试。天空盒不进网格，始终可见。
- `GeometrySystem` 只遍历 `Scene::visibleEntities`，不再遍历所有场景对象。


---

## 7. 着色器一览表

| 着色器 | 类型 | 属于 | 输入（描述符 / 顶点） | 输出 | 关键点 |
|--------|------|------|---------------------|------|--------|
| `mrt.vert` | vert | Geometry | 顶点 0-3（pos/color/normal/uv）+ Set 0 UBO + push（model/normal） | `fragColor/fragPosWorld/fragNormalWorld/fragUV` | 法线用 `mat3(normalMatrix)` |
| `mrt.frag` | frag | Geometry | 4 个 G-Buffer location + Set 1 的 4 张材质贴图 | 4 个附件 | 金属度取 `.b`、粗糙度取 `.g`（glTF 打包贴图）；切线空间法线函数**有意未启用** |
| `grid_mrt.vert/.frag` | vert/frag | Geometry | 同上 | 4 个附件 | `fwidth` 抗锯齿棋盘格 + 3000~10000 距离淡出。**当前无实体使用** |
| `skybox_mrt.vert` | vert | Geometry | 顶点位置 + Set 0 UBO + push | `outUVW`（局部方向） | `gl_Position = pos.xyww` 深度欺骗 + 加相机位置实现"跟随" |
| `skybox_mrt.frag` | frag | Geometry | `outUVW` + Set 0 binding 1（环境图） | albedo = 线性 HDR；normal = 0 | **不做 tonemap**（D-2 修正了双重 tonemap） |
| `fullscreen.vert` | vert | SSAO / Bloom / Present | 无顶点输入 | `outUV ∈ [0,1]` | 顶点索引生成全屏三角形，`vkCmdDraw(cmd, 3, 1, 0, 0)` |
| `ssao.frag` | frag | SSAO | Set 0 binding 2/3（Position/Normal）+ Set 1（UBO + 噪声） | `R8`：遮蔽因子 | **视图空间**比较深度；TBN 用噪声旋转；range check；天空输出 1.0 |
| `lighting.vert` | vert | Lighting | 无 | `outUV` | 与 `fullscreen.vert` 重复实现（历史原因） |
| `lighting.frag` | frag | Lighting | Set 0 binding 0~9（全部） | `hdrScene` | Cook-Torrance GGX + split-sum IBL + 点光源循环；`MAX_REFLECTION_LOD = 4.0`；**只输出线性 HDR** |
| `bloom_bright.frag` | frag | Bloom | 一张 sampler2D（`hdrScene`）+ push（threshold/knee/intensity） | `bloomLevels[0]` | Rec.709 亮度 + **soft knee** 二次过渡；高度 ~37 行 |
| `bloom_down.frag` | frag | Bloom | 一张 sampler2D（上一级）+ push（srcTexelSize/karisAverage） | 下一级 | 13 抽样 dual-filter；首级 Karis 加权抗萤火虫 |
| `bloom_up.frag` | frag | Bloom | 一张 sampler2D（粗一级）+ push（srcTexelSize/radius） | 细一级（**加法混合**） | 3×3 帐篷滤波（角 1/16、边 1/8、心 1/4） |
| `present.frag` | frag | Present | `hdrScene` + `bloomResult` + push（exposure/bloomIntensity/bloomEnabled） | 交换链 | ACES Filmic（Narkowicz 2015）+ `pow(1/2.2)`；**ACES 的 clamp 是整条链路的终点** |
| `ibl_capture.vert` | vert | IBLBaker（×4 共用） | 无 | `outUV` | 立方体面方向由 push 的 `faceIndex` 推出 |
| `ibl_equirect_to_cube.frag` | frag | IBLBaker | 等距柱状 sampler2D + push | `envCube` 某一面 | `v.y = +1` → `uv.y = 0`（HDR 顶部是天顶，已用脚本验证） |
| `ibl_irradiance.frag` | frag | IBLBaker | `envCube` samplerCube + push | `irradianceMap` | `sampleDelta = 0.025` 双重循环；`irradiance = π * Σ / N` |
| `ibl_prefilter.frag` | frag | IBLBaker | `envCube` samplerCube + push | `prefilteredMap` 某级 mip | Hammersley + GGX 重要性采样 + 采样立体角估 mip；**必须能 `textureLod`** |
| `ibl_brdf_lut.frag` | frag | IBLBaker | `inUV`（= NdotV, roughness） | `R16G16` (A, B) | 1024 采样；IBL 版 Smith 用 `k = a²/2`（与直接光不同） |
| ~~`light_cube.vert/.frag`~~ | — | — | — | — | **无人引用的死代码** |
| ~~`shader.vert/.frag`~~ | — | — | — | — | **无人引用的死代码**（早期教学 shader） |

**编译约定**：`#version 450`；push constant 的**字段顺序和大小必须与 C++ 侧结构体严格一致**（`BloomPass::BrightPush` / `DownPush` / `UpPush`、`PresentPass::PresentPush`、`IBLBaker::PushConstants`、`SimplePushConstantData`、`SSAOUbo`）。`GlobalUbo` 额外有 `static_assert(offsetof(...))` 在编译期守住 std140 布局。

---

## 8. 设计模式表

| 模式 | 位置 | 作用 | 备注 |
|------|------|------|------|
| **接口 / 策略** | `IRenderPass` | 把"一个渲染阶段"抽象成 `init()` + `addToGraph()` | 注意**没有** `execute(VkCommandBuffer, FrameInfo&)`，命令录制被渲染图接管 |
| **组合 / 有序容器** | `RenderPipeline` | `vector<unique_ptr<IRenderPass>>` + `initAll()` / `registerAll()` | 顺序即语义（Geometry → SSAO → Lighting → Bloom → Present） |
| **命令 / 延迟执行** | `RenderGraph::Pass::execute(std::function<void(VkCommandBuffer)>)` | 把"要画的命令"存起来，等所有屏障都推导完再统一录制 | 这是"声明 → 推导 → 录制"三段式的关键：屏障必须在**所有** pass 的声明都齐了之后才能算 |
| **构建器（数据装配）** | `Model::Builder` | 收集 vertices / indices / subMeshes 再一次性建缓冲 | `GLTFLoader` 和 `Model::Builder::loadModel` 共用 |
| **享元 / 缓存** | `MaterialSystem` 的 `textureCache` + `materials`（`unordered_map`） | 同一张贴图/材质只加载一次 | 用路径/名字做 key；glTF 材质按名字去重 |
| **对象池 + 空闲栈** | `DescriptorManager` 的 `materialSets` / `slotInUse` / `freeSlots` | 256 个 set 一次性申请；"分配/归还"只改内容 + 压/弹栈，O(1) | `freeSlots` 倒序压栈，所以第一次分配拿到槽位 0（方便调试对照） |
| **RAII** | 所有 RHI 类（`Device`/`Swapchain`/`Renderer`/`GBuffer`/`Texture`/`Pipeline`/`CubemapTexture`/`RenderGraph`）+ `IBLBaker`/`Scene` | Vulkan 句柄在析构里释放 | `RenderGraph` 只销毁 `owned == true` 的资源（导入的不碰） |
| **ECS** | EnTT `entt::registry` | 组件 + view 查询 + 轻量实体句柄 | D-7 起成为场景数据唯一事实来源 |
| **数据总线（帧上下文）** | `FrameInfo` | 每帧可变的东西打成一个结构体往下传 | 避免每个 Pass 都持有 `Scene*`；注意里面的 `settings`/`telemetry`/`registry`/`visibleEntities` 是**引用** |
| **单一事实来源** | `RenderResources`（资源句柄）+ `GBufferFormat`（格式）+ `kInvalidMaterialSlot`（哨兵）+ `AssetPaths.h`（路径） | 同一个事实只在一处定义 | 违反这条就会出 D-2 之前那种"注释说 SRGB、代码是 FLOAT"的问题 |
| **编译期契约** | `static_assert(offsetof(GlobalUbo, ...))` | 让 C++ 结构与 GLSL std140 布局在**编译期**对齐 | 改成员顺序会直接编译失败，而不是运行时画面错乱 |
| **单例式的"应用装配器"** | `FirstApp` 的构造函数 | 把创建顺序这件容易错的事集中在一处 | 代价是构造函数长；换来的是"顺序错了会立刻在这里暴露" |

---

## 9. 每帧数据流

```
main()
 └─ FirstApp::run()                              ← 无限循环
     ├─ glfwPollEvents()
     ├─ [最小化时] ImGui_ImplGlfw_Sleep(10) 后 continue
     ├─ 计算 frameTime (high_resolution_clock)
     ├─ update(dt)
     │   ├─ imguiSystem->newFrame()              （ImGui_ImplVulkan_NewFrame + Glfw_NewFrame + ImGui::NewFrame）
     │   └─ scene->update(dt, window, aspect)
     │       ├─ cameraController.processPCInput()  → cameraObject.transform
     │       └─ camera.setViewYXZ() + setPerspectiveProjection(60°, aspect, 0.1, 5000)
     └─ render(dt)
         ├─ renderer->beginFrame()               （等 fence → 取图像 → reset/begin 命令缓冲）
         │   └─ 返回 VkCommandBuffer（拿不到就整个跳过这一帧）
         ├─ scene->getTelemetry().reset()
         ├─ 组装 GlobalUbo（projectionView / cameraPos / 方向光 / exposure / numLights=0）
         │   └─ memcpy 到持久映射的 UBO + vmaFlushAllocation   ← 必须 flush
         ├─ 组装 FrameInfo（frameIndex = 交换链图像索引、dt、cmd、settings&、telemetry&、gameObjects&、矩阵、UBO、descriptorManager、cameraObj、cameraController）
         ├─ imguiSystem->prepareDrawData(frameInfo)   ← 建面板 + ImGui::Render()（当帧生效）
         ├─ renderGraph->beginFrame(cmd)              ← 清空上帧的 pass、复位瞬时资源布局
         ├─ renderPipeline.registerAll(graph, frameInfo)
         │   ├─ GeometryPass::addToGraph   → pass "Geometry"（4 色 + 深度，全 CLEAR）
         │   ├─ SSAOPass::addToGraph       → 上传 SSAOUbo + pass "SSAO"
         │   ├─ LightingPass::addToGraph   → pass "Lighting"（读 9 张，写 hdrScene）
         │   ├─ BloomPass::addToGraph      → pass "Bloom.Bright" + "Bloom.Down0..2" + "Bloom.Up2..0"
         │   └─ PresentPass::addToGraph    → pass "Present"（写交换链 + 画 ImGui）
         ├─ renderGraph->presentTransition(swapchainImages[frameInfo.frameIndex])
         ├─ renderGraph->endFrame()
         │   └─ 逐 pass：① 去重访问声明 ② 发屏障 ③ 需要则 vkCmdBeginRendering + 设 viewport/scissor
         │                ④ 调 execute 回调（真正的绘制） ⑤ vkCmdEndRendering ⑥ 记录最终状态
         └─ renderer->endFrame()                 （end 命令缓冲 → submit → present）
     └─ 每 5 秒：std::cout << "[Perf] FPS | draw calls | triangles | vertices"
```

**注册顺序 ≠ 声明顺序**：`registerAll` 按 `RenderPipeline` 里的顺序调 `addToGraph`，而 `addToGraph` 里的 `addGraphicsPass` 调用顺序决定了渲染图里的执行顺序。`BloomPass::addToGraph` 一次注册了 7 个 pass（bright → down → up，其中 up 是**由粗到细的逆序**），所以"一个 `IRenderPass` 对应多个渲染图 pass"是允许且被使用的。

**自检路径**：`--selftest [N]` 走的是**同一套** `update()` + `render()`（含 ImGui 构建与提交、present），只是把主循环换成固定 `dt = 1/60` 的 `for` 循环、不读窗口事件、跑完 N 帧后 `vkDeviceWaitIdle` 退出。这样"关掉某个 Pass"的分支也能进自动回归 —— 详见 6.4 结尾。

---

## 10. 关键设计决策

每条都写清取舍。**没有"只列结论"的条目。**

### 决策 1：用 Vulkan 1.3 动态渲染，彻底不要 `VkRenderPass`

**取舍**：放弃对 Vulkan 1.0/1.1/1.2 设备和移动端的支持，换掉的是渲染通道管理这一整类复杂度。

- **收益**：① 没有 `VkRenderPass` / `VkFramebuffer` 要建、要缓存、要随交换链图像数复制；② 不用处理 `initialLayout`/`finalLayout`/`subpass dependency` 这套隐式同步（它和渲染图的显式屏障**语义重叠**，两套一起用极易互相打架）；③ 一个 Pass 就是"一组 imageView + loadOp/clearValue"，这正好是 `VkRenderingInfo` 的形状，渲染图可以直接生成。
- **代价**：`vkCmdBeginRendering` 之后必须显式设 viewport/scissor（本项目把这两项做成所有图形管线的动态状态），而且**不能再用 subpass**（TBDR 上的 on-chip 复用没了）。桌面 GPU 上后者没有影响。
- **为什么这个取舍划算**：项目只有 Windows 桌面一个目标平台，用户已确认 GPU 支持到 1.4。

### 决策 2：线性渲染图，而不是完整 DAG

**取舍**：放弃自动重排 / 内存别名化 / 死代码剔除 / 跨帧资源池化，换来一个 400 行、能读懂、能调试的同步系统。

- **为什么"线性"够用**：手写屏障的 90% 痛点是"这次访问该是什么布局、上一次是什么布局"，而这个问题在"顺序执行 + 每张图记住上次状态"的模型下就完全解决了。
- **为什么不上 DAG**：DAG 需要拓扑排序、资源生命周期分析、别名内存布局、跨帧池化 —— 每一样都是独立的子系统，而且**错了以后极难调试**（"这个别名是不是踩了那个的生命周期"）。收益要等场景规模真的上来（几百个 Pass、显存吃紧）才体现。
- **D-2 的验证**：新增 9 个 Pass（1 亮部提取 + 3 降采样 + 3 上采样 + 1 Present + 1 PresentTransition）**没有写一行屏障代码**，全部只声明 `read` / `writeColor`。这是渲染图第一次被真正压力测试 —— 它扛住了，但同时也暴露了"漏声明不会报错"的风险，所以同一提交里补了两件事：**同一 Pass 内重复访问只保留最后一次声明**（Bloom 上采样），以及**声明合法性校验**（句柄越界 / usage 不匹配直接抛异常并报纹理名）。见 6.2、6.3。
- **保留的扩展位**：`TextureEntry` 已经记录了 `layout/stage/access` 和 `usage`（校验就是靠它能做的）；`passes` 已经是独立对象容器；`createTexture` 已经是唯一的资源创建入口（将来换成"从池里借"只改这一处）。

### 决策 3：渲染图用「访问声明」推导屏障，而不是「显式屏障 + 自动批处理」

**取舍**：Pass 作者必须**手动列出**自己读了哪些图（多写几行），换来"漏声明 = 漏同步"这个**响亮**的失败模式。

- **反过来的设计**是"Pass 自己写屏障，渲染图只做批处理/合并"。那样写错的表现是"屏障太少 → 偶发花屏"或"屏障太多 → 性能悄悄下降"，两者都难定位。
- **当前设计的失败模式**是验证层直接报 layout mismatch / sync hazard。**错误可见**比**错误少**更重要。
- **代价**：声明和 shader 之间有一份必须手工同步的清单（例如 `LightingPass::addToGraph` 里 9 条 `read`）。这份清单是"同步的唯一事实来源"，改 shader 忘了改它就会炸 —— 但会**立刻**炸。

### 决策 4：渲染图不管采样器，只管图像

**取舍**：使用方要自己建/持有采样器（`SSAOPass` 的 `outputSampler`、`BloomPass`/`PresentPass` 的 `sampler`、`GBuffer` 的 NEAREST 采样器、IBL 的三个），换来渲染图的接口不被"读的策略"污染。

- **理由**：采样器是"**怎么读**"（过滤/寻址/各向异性），和"**谁写了这张图**"（图要管的同步）是正交的。把采样器塞进图里会导致：同一张图被不同 Pass 用不同过滤方式读时，图得为一个 `VkSampler` 做冲突解决 —— 收益是省几行代码，代价是接口语义变模糊。
- **代价**：描述符集必须等到"图已经创建了 image view"之后才能写（所以 `buildGlobalSet` 在 `SSAOPass::init()` 之后才调），`Application` 的构造顺序因此有了硬依赖。

### 决策 5：`presentTransition` 由应用显式声明

见 6.5。这里的取舍是"渲染图放弃对帧的完整知识"，换来"离屏渲染 / 多窗口 / 截图 不用改渲染图"。

### 决策 6：光照输出线性 HDR，tonemap 独立成 PresentPass（D-2）

**取舍**：多一张全分辨率的 16F 中间目标（1920×1080×8B ≈ 16 MB 显存 + 相应的写/读带宽），换来后效链的正确性与可组合性。

- **不这么做会怎样**：Bloom 作用在已 clamp 的 LDR 上 = 只能"把白涂更白"，得到整体发灰；或者把 tonemap 留在光照通道、把 Bloom 塞进去 —— 那 Bloom 就无法共享，而且以后每加一个后效都要往 `lighting.frag` 里堆。
- **为什么曝光也要一起搬**：Bloom 阈值必须作用在与显示无关的线性量上。曝光放在光照里，阈值就随曝光漂移（拖滑块时辉光强度乱跳）。

### 决策 7：G-Buffer 的 Albedo 用 `R16G16B16A16_SFLOAT`（D-2 从 `R8G8B8A8_SRGB` 改）

**取舍**：albedo 通道的带宽和显存 ×2，换来天空盒 HDR 高光能活着进 Bloom。

- **为什么需要**：设计上让天空盒"借道 albedo 通道"把线性 HDR 辐照度交给 Lighting 透传。8bit 会在写入时截断到 1.0，而被截断的 1.0 **恰好超过 Bloom 阈值** → 整块天空都在起辉光 → 一层假雾。
- **另一个选项**（未来可以考虑）：给天空盒单独一个 HDR 附件，或者把天空从 G-Buffer 里拿出去单独画一遍。那样 albedo 就能回到 8bit。当前选择的是"更少的 pass"。

### 决策 8：Bloom 用 4 级金字塔，而不是单级大核模糊

**取舍**：7 个额外 pass + 4 张额外中间目标，换来 O(N) 的采样复杂度和**多尺度**光晕。

- 单级大核高斯是 O(r²)，而且只有一个尺度（近处和远处的辉光形态一样，看起来"糊"而不是"发光"）。
- 4 级是实测选定的：3 级在 1920×1080 下最大辉光半径偏小，5 级最粗一级只有 60×34，贡献几乎看不见但要多付两个 pass。
- **代价**：`kLevelCount` 与 `DescriptorManager` 的池余量（`+64`/`+96`）有隐式耦合，改级数要同步调整。

### 决策 9：上采样用加法混合叠加，不额外开累加缓冲

**取舍**：省下一张全分辨率缓冲，代价是"同一张图在一个 Pass 里既被读又被写"的语义（`LOAD_OP_LOAD` + `ONE/ONE` 混合）。

- 另一种做法是"ping-pong"两个缓冲（up 写 B 读 A，再反过来），显存 ×2，但每个 Pass 的数据流更单纯。
- 这里选省显存，因为渲染图保证"进入 Pass 时的布局正确"，而加法混合的语义（读回自己）在 Vulkan 里是完全合法的。

### 决策 10：IBL 一次性 GPU 烘焙 + split-sum，而不是运行时解析

**取舍**：初始化多花一次几十毫秒的 GPU 时间 + 约 2 MB 常驻显存，换来运行时零成本的物理正确环境光照。

- **烘焙而不是运行时积分**：irradiance 的半球卷积是几万次采样/纹素，运行时做不可能。
- **split-sum 而不是粗糙度系数近似**：老实现 `envSpecular *= (1.0 - roughness)` 能量完全不守恒（粗粗糙度时高光消失）。split-sum 把镜面积分拆成两个可预计算的项，运行时只有 2 次采样。实测过曝像素从 95.9% 降到 7.1%。

### 决策 11：IBLBaker 保留 `VkRenderPass`/`VkFramebuffer` 的死代码

这不是"设计"而是**迁移残留**：`IBLBaker` 在 D-1(b) 已经改成动态渲染（`renderFaces` / `renderBrdfLut` 都调 `vkCmdBeginRendering`），但 `createRenderPasses()` 变成了空函数、`makeFramebuffer()` / `createOffscreenRenderPass()` / `pendingFramebuffers` / `cubeRenderPass` / `lutRenderPass` 这些符号还留着。

保留了 `pendingFramebuffers` 的**注释**（"不能在录制完就销毁 Framebuffer"）作为历史教训的记录 —— 这条经验在动态渲染下依然适用（录制完就销毁 image view 一样是错的）。清理时建议只删代码、把注释移到合适的地方。

### 决策 12：材质槽位用「一次性预分配 + 空闲栈」，而不是动态描述符池

- **收益**：① 运行期不再有 `vkAllocateDescriptorSets`（也就不会池耗尽）；② "分配/归还"变成 O(1) 的栈操作；③ 材质槽位稳定（槽位号不会随重建而变），可以把槽位号直接放进 push constant / 顶点属性（这是 D-3 bindless 的地基）。
- **代价**：256 个槽位 + 1024 个 sampler 描述符**在启动时一次性占掉**，即使场景里只有几十个材质。在桌面 GPU 上这个量级的浪费可以忽略。
- **配套约定**：`kInvalidMaterialSlot = UINT32_MAX` 而不是 0（0 是合法槽位号）—— 见 AI_Readme 坑 4。

### 决策 13：glTF 几何在加载时烘焙到世界空间

`GLTFLoader` 遍历节点时累积变换，**直接把顶点变换到世界空间**再塞进统一的顶点缓冲；不保留节点层级、不保留 per-node 变换。

- **收益**：① 渲染路径完全不需要"每个子网格一个 model matrix"的概念（Sponza 有上百个节点，逐节点 push constant 是一笔开销）；② 顶点缓冲可以合并成一个（减少绑定切换）；③ 与 OBJ 路径产出的 `Model` 完全同构。
- **代价**：**失去了节点层级的动态性** —— 想让 Sponza 里某扇门动起来，现在做不到（几何已经烘焙死了）。这对"静态场景 + 相机漫游"的当前阶段是可接受的，但**对开放世界目标不行**：D-7（Transform 层级）必须把这部分重新设计（要么保留节点信息，要么在导入时生成实例化数据）。

### 决策 14：用 `RenderTagComponent` 标签而不是继承类型

`GeometrySystem::render` 里：

```cpp
auto* pipe = &geometryPipeline;
if (obj.isSkybox)    pipe = &skyboxPipeline;
else if (obj.isGrid) pipe = &gridPipeline;
```

- **收益**：极简，不需要多态/组件注册。
- **D-7 状态**：原来的 `GameObject::isSkybox/isGrid` 已迁移成 `RenderTagComponent`。
  标签仍然是互斥语义、类型上没有强制，但现在是可选组件，渲染系统通过 view 查询。

### 决策 15：`FrameInfo` 作为每帧数据总线，用引用传递可变状态

```cpp
struct FrameInfo {
    ...
    EngineSettings& settings;       // 引用
    RenderTelemetry& telemetry;     // 引用
    std::vector<GameObject>& gameObjects;  // 引用
    DescriptorManager* descriptorManager;  // 指针（可空）
    GameObject* cameraObj;                 // 指针（可空）
    CameraController* cameraController;    // 指针（可空）
};
```

- **收益**：Pass 不需要持有 `Scene*`，测试/换场景只要换 `FrameInfo`；ImGui 面板改了 `settings` 当帧就生效（因为引用指向 `Scene` 里的同一份对象）。
- **代价**：`FrameInfo` 是个"什么都往里塞"的袋子，没有编译期约束谁能改什么。`settings` 被 ImGui 和多个 Pass 共享读写，是隐式耦合。
- **一致性上的瑕疵**：成员混用引用和裸指针（可空的用指针），但**没有文档规定这条规则**。

### 决策 16：`Application` 的构造函数承担全部创建顺序

构造函数有 120 多行、10 个步骤，其中至少 4 条顺序是**硬依赖**（渲染图 → Pass；DescriptorManager → Scene；IBLBaker → buildGlobalSet；PresentPass → ImGuiSystem）。

- **收益**：所有顺序约束集中在一个地方，**顺序错了会立刻在这里暴露**（而不散落在各模块的懒初始化里）。
- **代价**：构造函数长；`Application.h` 包含了一堆头；没法单独测。
- **改进方向**：把这些步骤拆成 `createDeviceLayer()` / `createSceneLayer()` / `createRenderLayer()` 三个私有方法，并给每条硬依赖写注释说明"为什么必须在这个位置"。

### 决策 17：窗口不可缩放（当前）

- **收益**：所有中间目标、所有管线都不需要重建路径；`PipelineConfigInfo` 里的尺寸只在建管线时用一次（viewport/scissor 是动态状态，但 `BloomPass`/`PresentPass` 的描述符视图是 init 时写死的）。
- **代价**：这是个**会阻塞后续所有工作**的技术债 —— 地形/体素的 LOD、超采样、编辑模式都需要动态分辨率。
- **为什么现在不做**：真正的"可缩放"不只是重建交换链，而是"渲染图要能重建它拥有的资源（`ssao`/`hdrScene`/`bloomLevels`）+ 所有依赖这些 view 的描述符 set 要重写 + 所有管线要重建"。这需要渲染图支持资源重建（一个 `onResize` 路径），工作量不小，应该和 D-4（多帧并行）一起做，因为两者都要动帧资源的生命周期。

### 决策 18：`std::cout` / `std::cerr` 而不是日志库（当前）

spdlog 已经拉进 3rdParty 并配了 include 路径，但一行都没用。

- **现状的问题**：`[Sentinel]` / `[DescriptorManager]` / `[IBL]` / `[GLTF]` / `[Scene]` 这些前缀是手工拼的，没有级别（info/warn/error）区分 —— 验证层消息和普通日志都往 stderr 走，"stderr 0 字节"这条验收标准因此**顺带**也要求了"没有 `std::cerr` 输出"（`GLTFLoader` 的警告、`GeometrySystem` 的"没有有效材质槽位"都会污染它）。
- **建议**：要么在 D-3 之前把 spdlog 接上（顺手把验证层回调也分流到独立 logger），要么在验收流程里明确"stderr 只允许验证层消息"。

### 决策 19：配置用 nlohmann/json 存进工程根的 `config/`（W1）

- **为什么需要**：D-2 调 Bloom 时扫了四组参数，每组都要"改源码默认值 → 重编译 → 启动 → 截屏"。
  面板本来就能实时改，只是不落盘。这不是"加个好用的功能"，而是被实际开发节奏逼出来的。
- **为什么用 nlohmann/json 而不是自己写解析**：它**已经在树里**（`3rdParty/tinygltf/json.hpp`，
  tinygltf 拉取时带下来的），零额外依赖、零联网成本。自己写一个解析器省不下任何东西，
  还要自己处理转义/数字格式/容错。
- **为什么放工程根而不是 `build/`**：与资源路径同源的理由 —— 不能依赖工作目录；
  而且放工程根能让 Debug / Release 共享同一份用户设置，并让这份文件进版本库当默认预设。
- **容错取向**：文件不存在 / JSON 语法错 / 单个字段类型不对，**都不抛异常**。
  缺失字段保持编译期默认值并记一条警告 —— 手改坏一个字段不该让引擎起不来。
- **原子写**：先写 `.tmp` 再 rename。直接覆盖写的话，写到一半崩溃会留下语法坏掉的配置，
  下次启动直接报错。
- **只持久化"设置"，不持久化"场景"**：`EngineSettings` + 相机位姿进配置；场景/模型/材质不进。
  后者是**资源**不是**设置**，恢复它们涉及资产路径、加载顺序、失败回退，是另一件事。
- **UI 文字必须是 ASCII**：ImGui 内置字体只有 ASCII 字形，中文会整条渲染成 `???`。
  所以 `EngineConfig::getLastStatus()` 返回的是英文 —— 这是刻意的，不是遗漏。
- **代价 / 局限**：没有版本迁移逻辑（`version` 字段写进去了但还没用）；
  没有"多套预设"（以后做画质档位需要扩展成目录 + 名字）。

### 决策 20：把"瞬时资源必须有人写"做成渲染图的硬校验（W2）

- **为什么**：渲染图保证**同步**正确性，但**语义**正确性（下游读的图这帧有没有人写）
  完全没有检查。验证层对这类问题一言不发（从 `UNDEFINED` 转出来是合法的）。
  SSAO 那个洞就是踩在这里，而且是靠人读代码才发现的。
- **为什么只需顺序遍历**：这是**线性**渲染图（决策 2）—— Pass 严格按声明顺序执行，
  顺序扫描的结论就是准确的，不需要真正的 DAG 分析。这一条让校验的实现只有几十行。
- **为什么报错要点名"本该由谁写"**：只说"没人写过"，读者还得自己翻是谁该写。
  直接告诉它"SSAO 本来由 Pass X 写，而 X 这一帧被跳过了"，才是可行动的报错。
- **为什么给常驻资源豁免**：IBL 烘焙结果这类图整帧只读是正常状态。
  豁免依据是 `TextureDesc::persistent`，而 `importTexture` 会把
  "初始布局 ≠ `UNDEFINED` ⇒ 常驻"的结论**回写**进它 —— 同一事实只能有一个来源。
- **代价**：它逼着两个 Pass 改了行为（Bloom 关闭从"跳过 Pass"改成"强度置零"；
  `importTexture` 的常驻判定回写）。关掉 Bloom 仍要跑 7 个全屏 Pass。
  我认为这笔交易划算 —— 见 6.4b 的详细理由。
  > 但这个代价**我自己并不满意**，已登记进技术债（`AI_Readme.md` 第 12 节第 17 条）：
  > 更好的做法是 PresentPass 准备两套描述符（开/关各一套），关闭时干脆不绑
  > `bloomResult`，于是 Pass 可以真正跳过。没做的原因是描述符要改成每帧重写，
  > 而收益（省 7 个半分辨率 Pass）暂时不值那份复杂度。
  > **重新权衡的时机**：Bloom 变重时（多级金字塔 / compute 版 / 自动曝光接它的 mip 链）。
- **它覆盖不到的地方**：只管"图"，不管"buffer"和"单个 mip/层"。
  D-5 引入 storage buffer 时必须把这个校验一起扩展，否则新资源类型又回到人工检查。

### 决策 21：材质描述符 bindless（D-3）

- **为什么做**：Sponza 103 个子网格，每次切材质都要 `vkCmdBindDescriptorSets` ——
  104 次 draw 伴随最多 104 次绑定。bindless 之后**整帧只绑一次 set 1**（实测 `desc binds` 1），
  切材质只是改 push constant 里的一个整数。
- **为什么是"一个 set 里 4 个纹理数组"而不是"每材质一个 set"**：这是 bindless 的定义。
  描述符数组 + 运行时索引让一次绑定覆盖全部材质，是后续实例化 / GPU-driven 的前提。
- **为什么 4 个独立数组而不是一个 struct 数组**：GLSL 不允许把 `sampler2D` 放进结构体数组
  再索引。拆开最省事，而且四个数组共用同一个下标，读起来反而更清楚。
- **为什么用变长数组**（`VARIABLE_DESCRIPTOR_COUNT`）：想让材质容量可调而不用维护多套布局。
  代价是多了两个必填开关，而且这个标志**只能加在编号最大的 binding 上**，
  分配长度还必须 ≤ 布局里声明的 `descriptorCount`（那是上限，不是最终长度）。
  这三条都是验证层当场拦下来的，见 `AI_Readme.md` 第 6 节的表。
- **为什么 `materialValid` 走 push constant**：天空盒 / 程序化网格不采材质数组，
  它们的槽位可能压根没写过内容。`descriptorBindingPartiallyBound` 允许数组尾部未绑定，
  但**采它就是未定义行为** —— 所以显式传一个有效性标记，而不是"祈祷槽位都有内容"。
- **收益 / 代价**：FPS 452 → 498（+10%），绑定次数 104 → 1。
  **draw call 数不变**（104）—— bindless 省的不是 draw call，是绑定与状态切换。
  为此专门加了 `desc binds` 计数器：没有它就没法证明"整帧只绑一次"真的生效。
- **新增的约束（重要）**：bindless 之后"回收槽位再复用"意味着**改写 GPU 可能正在读的
  数组项**。当前只在初始化阶段分配（设备此时空闲）所以安全；
  将来要在运行期动态分配材质，必须加帧 fence 或上 `UPDATE_AFTER_BIND` + 双缓冲。
  这条已经写进 `DescriptorManager::releaseMaterialSet` 的注释。

---

## 11. 附：架构上的已知薄弱点（按影响排序）

> 当前可执行的技术债清单与验收标准见 `TECH_DEBT.md`；本节保留架构层面的历史说明。

| # | 薄弱点 | 影响 | 建议时机 |
|---|--------|------|---------|
| 1 | ~~`Renderer` 单缓冲 + 单份 UBO~~ → **D-4 已修**：2 帧在飞，命令缓冲 / timeline / UBO / 瞬时图 / 描述符集均按 frame slot 隔离 | ✅ 已完成 |
| 2 | 窗口不可缩放 | 阻塞动态分辨率 / 超采样 / LOD | D-4 后单独安排 |
| 3 | 材质槽位没有真正的回收调用方 | `releaseMaterialSet()` 是死 API；场景反复加载会慢慢吃满 256 槽 | D-3 前 |
| 4 | ~~渲染图不支持 buffer，compute 路径只有骨架~~ → **D-5 已支持 storage buffer、compute command buffer 与异步 compute queue**；跨队列 producer/consumer 调度仍待补 | GPU culling / 体素任务接入前处理跨队列依赖 |
| 5 | ~~瞬时资源"必须每帧都有人写"~~ → W2 覆盖 image，**D-5 已扩展到 buffer**；单个 mip/层仍未细分 | mip/层独立读写时可能有盲区 | 有需求时补 |
| 6 | glTF 几何烘焙到世界空间，丢失内部节点层级；entity 级 Transform 已可用，但不能单独驱动 Sponza 内部节点 | 阻塞局部动态物体 / 实例化；开放世界资产需要保留 node-instance 层级 | 地形/体素主功能前或资产管线升级时 |
| 7 | `LightingPass` 兼任"导入交换链" | Pass 职责不对称，init 顺序有隐式依赖 | 清理类 |
| 8 | 遗留代码：`Descriptor.h/.cpp`（**有意保留**，文件头已注明）、`light_cube.*`、`shader.*`、`Swapchain` 的 depth/Framebuffer、`IBLBaker` 的 RenderPass/Framebuffer 残留 | 增加阅读成本 | 清理类 |
| 9 | 自检开关只覆盖 bloom / ssao 两个 Pass | 新增可关闭的 Pass 时，那条分支就是验证盲区（要照第 3.6 节加参数） | 加新 Pass 时照做 |
| 10 | `EngineConfig` 只持久化 `EngineSettings` + 相机位姿 | 场景/模型/材质等"资源类"状态不落盘（这是刻意的 —— 见决策 18，但将来要加"恢复上次场景"就得扩展） | 有需求时 |
| 11 | **"关闭 Bloom"仍有 7 个全屏 Pass 的固定开销**（W2 引入） | 关掉 Bloom 并不省 GPU；在低端卡/移动端会显得"关了没变快" | Bloom 变重时改两套描述符绑定 |
| 12 | 验证脚本的两个前提（正常关窗 / 窗口在前台）没有自动化保护 | 脚本在 `%TEMP%` 下、不在版本库；换机器要重写，写错就会得到"看着正常其实无效"的结论 | 把脚本迁进仓库 |
| 15 | JobSystem 目前只有全局队列 / 无 work stealing / 无任务依赖图；worker 内 nested parallelFor 顺序退化 | 大量短任务时吞吐和负载均衡受限，递归任务无法并行展开 | terrain / voxel 接入后按 profile 升级 |
| 16 | D-7 的 glTF 内部 node 层级仍然被烘焙掉；空间网格每帧重建 | 局部动态物体不可动；对象多时网格重建成本会上升 | 地形/体素主功能前或资产管线升级时 |
| 13 | bindless 的 `normalMaps` 数组绑了但没采样（D-3） | 每个材质白占 1/4 的描述符与一次更新；256 材质 = 256 个无用描述符 | 启用切线空间法线贴图时 |
| 14 | ~~运行期复用材质槽位会改写 GPU 可能正在读的数组项~~ → **D-4 已修**：归还前 `vkDeviceWaitIdle`。若回收进热路径再改成 timeline 延迟回收 | 性能优化时再升级 |

> 曾经排在第 8 位的「`FirstApp` 析构顺序」已经在 W1 修掉（`25ae42f`），
> 而且它比看上去严重：`renderPipeline` 是值成员、声明得比 `vulkanDevice` 晚，
> 于是 Pass 持有的 VMA 分配在 `vmaDestroyAllocator` 之后才释放，直接触发 VMA 断言。
> 它一直没暴露，是因为验证脚本用 `TerminateProcess` 杀进程、根本不跑析构 ——
> **"验证层 0 字节"这个结论以前从未覆盖退出路径**。详见 `AI_Readme.md` 坑 15。

> 这些薄弱点的详细证据（文件/行/函数）写在 `AI_Readme.md` 第 12 节。
>
> 曾经排在第 1 位的「`SSAOPass` 关闭时 `ssao` 图内容未定义」已在 `3fed268` 修掉：
> 开关下沉到着色器（`enabled` 判断提到最前面，关闭时直接输出中性白），
> Pass 永远执行。它之所以值得在这里留一句，是因为**它揭示的是一类问题而不是一个 bug**：
> 渲染图的同步推导是正确的，但"下游无条件读的瞬时图是否每帧都有人写"这层
> **语义**约束完全没有机器检查。`--selftest [--no-bloom] [--no-ssao]` 是目前的补丁式护栏。
