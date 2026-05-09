# VulkanEngine — "Sentinel" 架构文档

## 1. 项目概览

| 项目 | 详情 |
|------|------|
| 语言标准 | C++17 |
| 图形API | Vulkan 1.0 |
| 窗口库 | GLFW3 (无边框/不可缩放, 1920×1080) |
| 数学库 | GLM |
| 模型加载 | tinyobjloader (OBJ) |
| 纹理加载 | stb_image (LDR + HDR) |
| Debug GUI | Dear ImGui (Vulkan + GLFW 后端) |
| Shader编译器 | glslangValidator (GLSL → SPIR-V) |
| 呈现模式 | `VK_PRESENT_MODE_IMMEDIATE_KHR` (无垂直同步) |

---

## 2. 目录树

```
VulkanEngine/
├── CMakeLists.txt
├── main.cpp
├── 3rdParty/
│   ├── glfw/               # 窗口 & 输入
│   ├── glm/                # 数学库
│   ├── imgui/              # 调试 GUI
│   ├── stb/                # 纹理加载 (stb_image)
│   └── tinyobjloader/      # OBJ 模型解析
├── shaders/
│   ├── fullscreen.vert      # 全屏三角形 VS
│   ├── grid_mrt.vert/frag   # 程序化无限网格
│   ├── light_cube.vert/frag # 调试光源球体 (暂未使用)
│   ├── lighting.vert/frag   # 延迟光照合成
│   ├── mrt.vert/frag        # 主几何体 MRT 填充
│   ├── skybox_mrt.vert/frag # 天空盒 MRT 输出
│   ├── ssao.frag            # SSAO 计算
│   └── *.spv                # 编译后的 SPIR-V
├── Resources/
│   ├── Models/
│   │   ├── DamagedHelmet/  # glTF 头盔 (PBR 贴图)
│   │   ├── Sponza/          # 未使用的占位
│   │   └── *.obj            # 各种 OBJ 模型
│   └── Texture/
│       ├── environment.hdr        # HDR 环境贴图
│       ├── environment_indoor.hdr # 备用 HDR
│       ├── *.jpg / *.png          # PBR 贴图
│       └── RuslLessRL/ / RustLessRH/ / RustedMetal/  # 更多 PBR 贴图集
└── src/
    ├── Core/
    │   ├── Application.h/.cpp     # 引擎主控: FirstApp
    │   ├── Descriptor.h/.cpp      # DescriptorAllocator + DescriptorBuilder
    │   ├── FrameInfo.h            # 每帧数据结构
    │   ├── ImGuiSystem.h/.cpp     # ImGui 调试面板
    │   ├── Material.h             # PBR 材质定义
    │   └── MaterialSystem.h       # 材质/纹理缓存管理
    ├── Render/
    │   ├── GeometryPass.h/.cpp    # 延迟几何通道
    │   ├── GeometrySystem.h/.cpp  # 几何渲染系统 (含3个Pipeline)
    │   ├── IRenderPass.h          # 渲染通道抽象接口
    │   ├── LightingPass.h/.cpp    # 延迟光照合成通道
    │   ├── LightingSystem.h/.cpp  # 光照合成系统
    │   ├── PostProcessPass.h/.cpp # 后处理通道基类
    │   ├── RenderPipeline.h       # 通道管理器/容器
    │   └── SSAOPass.h/.cpp        # 屏幕空间环境光遮蔽
    ├── RHI/
    │   ├── Device.h/.cpp          # Vulkan 实例/物理/逻辑设备封装
    │   ├── GBuffer.h/.cpp         # 延迟渲染 G-Buffer (5附件)
    │   ├── Pipeline.h/.cpp        # 图形管线封装
    │   ├── Renderer.h/.cpp        # 帧生命周期 (beginFrame/endFrame)
    │   ├── Swapchain.h/.cpp       # 交换链 + 深度缓冲
    │   └── Texture.h/.cpp         # 纹理加载 (LDR & HDR)
    └── Scene/
        ├── Camera.h/.cpp          # VulkanCamera: 投影 + 视图矩阵
        ├── CameraController.h/.cpp # FPS 风格摄像机输入
        ├── GameObject.h           # ECS 风格游戏对象
        └── Model.h/.cpp           # 顶点/索引缓冲 + OBJ 加载
```

---

## 3. 渲染架构

### 3.1 核心方案: 延迟着色 (Deferred Shading + MRT)

几何通道一次性写入 4 个颜色目标 + 深度，光照通道以单次全屏绘制完成。

#### G-Buffer 布局 (5 个附件)

| 槽位 | 格式 | 内容 |
|------|------|------|
| Position | `R16G16B16A16_SFLOAT` | 世界空间位置 |
| Normal | `R16G16B16A16_SFLOAT` | 世界空间法线 |
| Albedo | `R8G8B8A8_UNORM` | 基础颜色 (sRGB 解码) |
| PBR | `R8G8B8A8_UNORM` | R: 金属度, G: 粗糙度 |
| Depth | `D32_SFLOAT` (或类似) | 深度缓冲 |

### 3.2 管线阶段

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. GEOMETRY PASS  (GeometryPass → GeometrySystem)               │
│    ├─ geometryPipeline  (mrt.vert + mrt.frag)   标准PBR物体     │
│    ├─ gridPipeline      (grid_mrt + grid_mrt)   程序化无限网格  │
│    └─ skyboxPipeline    (skybox_mrt + skybox_mrt)   天空盒      │
│    输出: G-Buffer (4 × Color + Depth)                           │
├─────────────────────────────────────────────────────────────────┤
│ 2. SSAO PASS  (SSAOPass → extends PostProcessPass)              │
│    输入: G-Buffer Position + Normal                             │
│    输出: 单通道 R8_UNORM 遮蔽贴图                                │
│    算法: 16 半球随机采样 + 4×4 噪声旋转 + Range Check            │
├─────────────────────────────────────────────────────────────────┤
│ 3. LIGHTING PASS  (LightingPass → LightingSystem)               │
│    输入: 全部 G-Buffer + SSAO                                   │
│    计算:                                                        │
│      ├─ 方向光 (Cook-Torrance BRDF + GGX)                       │
│      ├─ 点光源 (最多 100 个, 球面衰减)                           │
│      ├─ IBL 漫反射 + 镜面反射 (球面映射 HDR)                     │
│      ├─ ACES Filmic 色调映射                                    │
│      └─ Gamma 校正 (1/2.2) + SSAO 应用                          │
│    输出: 交换链图像 (PRESENT_SRC_KHR) + ImGui 叠加              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. 设计模式

| 模式 | 使用位置 | 说明 |
|------|---------|------|
| **策略/接口** | `IRenderPass` | 所有渲染通道的抽象基类: `init()` / `execute()` / `onResize()` |
| **组合/管线** | `RenderPipeline` | 有序持有 `unique_ptr<IRenderPass>`，逐通道执行 |
| **Builder** | `DescriptorBuilder` | 流式 API 构建 Vulkan 描述符集 |
| **对象池** | `DescriptorAllocator` | 每帧按交换链图像索引回收描述符池 |
| **RAII** | 所有 RHI 类 | Device / Swapchain / Renderer / GBuffer / Texture / Pipeline 均在构造函数/析构函数管理 Vulkan 资源 |
| **静态工厂** | `GameObject::createGameObject()` | 自动递增 ID |
| **ECS-lite** | `GameObject` + `TransformComponent` | 游戏对象持有 Transform / Model / Material + 标志位 |
| **缓存** | `MaterialSystem` | 统一纹理/材质缓存，避免重复加载 |
| **数据总线** | `FrameInfo` | 所有每帧数据 (指令缓冲/游戏对象/矩阵/纹理信息/设置/遥测) 打包为单一结构体 |

---

## 5. 每帧数据流

```
main() → FirstApp::run()
  └── 主循环:
       ├─ glfwPollEvents()
       ├─ 计算 frameTime (dt)
       ├─ update(dt): 摄像机输入, 投影矩阵
       ├─ render(dt):
       │    ├─ renderer->beginFrame()   → 获取图像, 重置Fence, 开始录制
       │    ├─ 重置当前交换链图像的描述符池
       │    ├─ 构建 GlobalUbo (摄像机/光源) → memcpy 到映射内存
       │    ├─ 构建 FrameInfo 数据包
       │    ├─ 遍历 renderPipeline:
       │    │    ├─ GeometryPass::execute()  → 开始 GBuffer RP, 绘制物体, 结束 RP
       │    │    ├─ SSAOPass::execute()      → 开始 SSAO RP, 全屏绘制, 结束 RP
       │    │    └─ LightingPass::execute()   → 开始交换链 RP, 全屏绘制 + ImGui, 结束 RP
       │    └─ renderer->endFrame() → 结束录制, 提交, 呈现
       └─ 直到窗口关闭
```

---

## 6. 核心模块详解

### 6.1 入口: `main.cpp`

- 设置 UTF-8 控制台编码 (Windows)
- 创建 `FirstApp`，调用 `app.run()`
- 捕获异常做友好报错

### 6.2 Core 层 (`src/Core/`)

#### `FirstApp` (Application.h/.cpp) — 引擎总控

| 职责 | 方法 |
|------|------|
| 窗口创建 | `initWindow()` — GLFW 1920×1080, 不可缩放, 无 OpenGL |
| 描述符布局 | `createDescriptorSetLayout()` — 单一大型 Set 0: UBO + 5 PBR 纹理 + 5 输入附件 |
| Uniform 缓冲 | `createUniformBuffers()` — 单个 Host-Visible UBO |
| PBR 纹理加载 | `loadAllPBRTextures()` — DamagedHelmet PBR + HDR 环境贴图 |
| 场景加载 | `loadGameObjects()` — 加载 OBJ 并创建 GameObject |
| 主循环 | `update(dt)` + `render(dt)` |

**成员变量**: Device / Swapchain / Renderer / GBuffer (unique_ptr), RenderPipeline (值), std::vector\<GameObject\>, 所有 Texture (shared_ptr), ImGuiSystem

#### `FrameInfo.h` — 关键数据结构

| 结构体 | 内容 |
|--------|------|
| `PointLight` | `vec4 position` (w=半径) + `vec4 color` (w=强度) |
| `GlobalUbo` | projectionView矩阵, 环境光, 方向光, 摄像机位置, 点光源数组 (最多100个) |
| `SimplePushConstantData` | modelMatrix + normalMatrix |
| `EngineSettings` | SSAO 半径/偏移/核大小, 方向光方向/颜色, 曝光度 |
| `RenderTelemetry` | 绘制调用数, 三角形数, 顶点数 |
| `FrameInfo` | **每帧全数据总线** — 帧索引/dt/指令缓冲/描述符分配器/设置/遥测/游戏对象/矩阵/UBO/全部纹理描述符信息 |

#### `DescriptorAllocator` + `DescriptorBuilder` (Descriptor.h/.cpp)

- **Allocator**: 管理多个 `VkDescriptorPool`，按交换链图像批量重置
- **Builder**: 流式构建器，用法 `DescriptorBuilder::begin(allocator, layout).bindBuffer(...).bindImage(...).build(set)`

#### `Material` / `MaterialSystem` (Material.h, MaterialSystem.h)

- **Material**: PBR 参数 (baseColorFactor, metallicFactor, roughnessFactor, alphaCutoff, alphaMode) + 智能指针纹理引用
- 静态默认纹理: 白色/黑色/平面法线(纯蓝) — 材质缺少贴图时回退使用
- **MaterialSystem**: `unordered_map<string, shared_ptr<Texture>>` 缓存 + `getOrCreateMaterial()`

> 注: MaterialSystem 已定义但当前渲染循环直接硬编码 DamagedHelmet 纹理，尚未完全集成。

#### `ImGuiSystem` (ImGuiSystem.h/.cpp)

- 使用 GLFW + Vulkan 后端初始化 Dear ImGui
- `render()` 显示 "Sentinel Engine Core" 调试面板:
  - 性能遥测 (FPS, 绘制调用, 三角形, 顶点)
  - SSAO 可调滑块 (半径, 偏移, 采样数)
  - PBR 光照控制 (曝光, 日光方向, 日光颜色)
  - 摄像机位置/旋转拖拽控制

### 6.3 RHI 层 (`src/RHI/`)

#### `Device` (Device.h/.cpp)
- 封装 Vulkan Instance / Physical Device / Logical Device / Surface / Queue
- 启用 `VK_LAYER_KHRONOS_validation` (Debug 构建)
- 扩展: `VK_KHR_SWAPCHAIN` + `VK_EXT_DEBUG_UTILS`
- 工具方法: `findMemoryType()`, `findSupportedFormat()`, `findDepthFormat()`

#### `Swapchain` (Swapchain.h/.cpp)
- `VK_PRESENT_MODE_IMMEDIATE_KHR` (允许撕裂, 低延迟)
- 按图像创建 ImageViews 和深度资源
- `createFramebuffers(VkRenderPass)`: 为每个交换链图像创建帧缓冲

#### `Renderer` (Renderer.h/.cpp) — 帧生命周期
- `beginFrame()`: 等待 in-flight Fence → 获取下一张图像 → 重置指令缓冲 → 开始录制
- `endFrame()`: 结束录制 → 提交 (with Semaphore) → Present
- 同步对象: 1 Fence + 2 Semaphores (imageAvailable, renderFinished)

#### `GBuffer` (GBuffer.h/.cpp)
- 5 个附件统一管理: Position / Normal / Albedo / PBR / Depth
- 所有附件带 `VK_IMAGE_USAGE_SAMPLED_BIT` (用于后续采样)
- NEAREST 采样器 (G-Buffer 纹素间无需插值)

#### `Pipeline` (Pipeline.h/.cpp)
- 封装 `VkPipeline` + `VkShaderModule`
- 加载预编译 `.spv` 文件
- `defaultPipelineConfigInfo()`: Triangle List, Fill, No Cull, Clockwise Front, Depth Test LESS

#### `Texture` (Texture.h/.cpp)
- 双加载路径: `loadLDR()` (stbi_load, RGBA8) / `loadHDR()` (stbi_loadf, RGBA32F)
- 标准 Staging Buffer 传输: Staging → 图像布局转换 (UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY)
- 各向异性过滤 (16x max), Repeat 寻址, Linear 过滤

### 6.4 Render 层 (`src/Render/`)

#### `IRenderPass` — 抽象接口
```cpp
virtual void init() = 0;
virtual void execute(VkCommandBuffer, FrameInfo&) = 0;
virtual void onResize(VkExtent2D) = 0;
virtual const std::string getName() const = 0;
```

#### `RenderPipeline` — 通道容器
- 持有 `std::vector<std::unique_ptr<IRenderPass>>`
- `addPass()` / `initAll()` / `resizeAll()`

#### `GeometryPass` + `GeometrySystem`
- **GeometryPass**: 创建自定义 RenderPass (4 颜色 + 深度), 管理 Framebuffer
- **GeometrySystem**: 持有 3 个 Pipeline:
  - `geometryPipeline` — 标准 PBR 物体
  - `skyboxPipeline` — 天空盒 (背面剔除关闭, 深度写入关闭, 深度比较 LESS_OR_EQUAL)
  - `gridPipeline` — 程序化网格
- 使用 Push Constants 传递每个物体的 modelMatrix / normalMatrix
- 根据 `isSkybox` / `isGrid` 标志选择 Pipeline

#### `PostProcessPass` — 后处理基类
- 创建渲染目标 (VkImage + ImageView + Sampler), 单附件 RenderPass, Framebuffer
- `onResize()`: 销毁并重建渲染目标

#### `SSAOPass` — 屏幕空间环境光遮蔽
- 输出格式: `VK_FORMAT_R8_UNORM`
- 拥有本地描述符集 (Set 1): SSAO UBO + 噪声纹理
- `generateSampleKernel()`: 16 个半球的随机采样 (靠近中心更密集)
- `createNoiseTexture()`: 4×4 RGBA32F 随机旋转向量
- 读取 G-Buffer Position / Normal 通过全局 Set 0

#### `LightingPass` + `LightingSystem`
- **LightingPass**: 创建交换链图像的 RenderPass (finalLayout = `PRESENT_SRC_KHR`), 每交换链图像一个 Framebuffer
- 构建 Set 0 描述符: UBO + 环境贴图 + G-Buffer全部附件 + SSAO
- 委托 LightingSystem 全屏绘制，然后调用 ImGuiSystem 渲染调试 UI
- `renderDebugLights()` 和 `light_cube` 着色器已实现但当前未启用

### 6.5 Scene 层 (`src/Scene/`)

#### `VulkanCamera` (Camera.h/.cpp)
- `setPerspectiveProjection(fovy, aspect, near, far)`: 手动构建透视矩阵 (Vulkan 习惯: Y 倒置, 深度 0-1)
- `setViewYXZ(position, rotation)`: 欧拉角相机, YXZ 旋转顺序, 手动计算正交基 (u, v, w)

#### `CameraController` (CameraController.h/.cpp)
- FPS 风格: 鼠标右键拖拽环视, WASD+EQ 移动
- 速度: 5.0 单位/秒, 视角灵敏度: 1.0
- `processAndroidTouchInput()`: 空桩 (未来移动端预留)

#### `GameObject` (GameObject.h)
- **TransformComponent**: translation / rotation / scale → `mat4()` = T×Ry×Rx×Rz×S
- **GameObject**: Move-only 类型, 自增 ID, shared_ptr\<Model\>, TransformComponent, 标志位 (isSkybox / isGrid), 可选 Material
- 工厂方法: `static GameObject createGameObject()`

#### `Model` (Model.h/.cpp)
- **Vertex**: position(vec3), color(vec3), normal(vec3), uv(vec2) + Vulkan 绑定/属性描述 (4个属性, location 0-3)
- **Builder**: 顶点/索引向量, `loadModel()` 使用 tinyobjloader 解析 OBJ, 哈希表去重顶点, 翻转 UV-Y
- **Model**: 顶点缓冲 (Host-Visible), 索引缓冲 (UINT32)
- `bind()` + `draw()`: 绑定缓冲 + 索引/非索引绘制

---

## 7. 着色器一览

| 着色器 | 类型 | 作用 |
|--------|------|------|
| `mrt.vert/.frag` | 几何通道 | 变换到世界/裁剪空间, 输出世界位置/法线/UV; 写入4个G-Buffer目标 |
| `grid_mrt.vert/.frag` | 网格 | 程序化棋盘格, 反走样边缘, 距离渐变淡出 |
| `skybox_mrt.vert/.frag` | 天空盒 | 本地坐标作为3D方向, Z=W 深度技巧; 球面映射 HDR → ACES + Gamma |
| `fullscreen.vert` | 后处理 | 从顶点索引生成全屏三角形 (无顶点缓冲) |
| `ssao.frag` | SSAO | TBN 空间半球采样 + Range Check, 输出单通道遮蔽因子 |
| `lighting.vert/.frag` | 光照合成 | GGX BRDF + Cook-Torrance + 方向光/点光源/IBL + ACES + Gamma + SSAO |
| `light_cube.vert/.frag` | 调试 | 实例化光源球体可视化 (暂未启用) |

---

## 8. 关键设计决策

1. **延迟着色 + MRT**: 几何与光照解耦，场景复杂度不影响光照开销
2. **每帧描述符池回收**: DescriptorAllocator 按交换链图像维护池，批量重置避免碎片化
3. **单一大型描述符集 (Set 0)**: 所有通道共享一个布局，各通道按需取用不同绑定槽位
4. **Push Constants 传递逐物体数据**: modelMatrix/normalMatrix 通过 `VkPushConstantRange` 推送，避免逐物体 UBO
5. **RAII 资源管理**: 所有 Vulkan 句柄封装在类中，资源由析构函数自动释放
6. **可插拔通道架构**: `IRenderPass` + `RenderPipeline` 支持随时添加新后处理通道 (如 Bloom, SSR)
7. **Host-Visible 顶点/索引缓冲**: 简单但非最优；静态几何应使用 Device-Local + Staging
8. **硬编码 1920×1080**: 通过 `#define` 全局定义，窗口不可缩放
9. **Material 系统未完全集成**: Material.h / MaterialSystem.h 已定义完整 PBR 材质体系, 但当前渲染直接硬编码纹理
10. **HDR 球面环境映射**: RGBA32F 等距矩形 HDR, IBL 使用相同环境贴图做漫反射/镜面反射的粗糙度近似模糊
11. **手写透视矩阵**: 匹配 Vulkan 坐标系 (Y 向下, 深度 0-1), 未使用 `glm::perspective`
12. **ACES Filmic 色调映射**: 光照和天空盒着色器均使用 Academy Color Encoding System 近似曲线
