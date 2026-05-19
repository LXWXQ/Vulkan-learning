# AI_Readme — VulkanEngine "Sentinel"

> 给 AI（或新开发者）的必读文档。读完即可理解整个项目并继续开发。

---

## 1. 项目概览

**VulkanEngine "Sentinel"** 是一个基于 Vulkan 的实时渲染引擎，采用延迟着色 + MRT 架构，支持 PBR 材质、SSAO、IBL 环境光照、HDR 色调映射。

| 项目 | 值 |
|------|-----|
| 语言 | C++17 |
| 图形 API | Vulkan 1.0 |
| 窗口 | GLFW3, 1920×1080 不可缩放 |
| 构建 | CMake 3.15+ |
| 数学库 | GLM (header-only) |
| 模型格式 | OBJ (tinyobjloader), glTF 待接入 |
| 纹理加载 | stb_image (LDR + HDR) |
| 显存管理 | VMA (VulkanMemoryAllocator) |
| Debug GUI | Dear ImGui (Vulkan + GLFW 后端) |

---

## 2. 快速构建

### 依赖路径（硬编码，新电脑需修改 `CMakeLists.txt`）

```
Vulkan SDK:   D:/work/vulkan/
glslangValidator: D:/work/vulkan/Bin/glslangValidator.exe
```

### 构建命令

```bash
cd VulkanEngine
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Debug
```

Shader 会在 pre-build 步骤自动编译（`.vert`/`.frag` → `.spv`）。

---

## 3. 目录结构

```
VulkanEngine/
├── CMakeLists.txt
├── main.cpp                          # 入口
├── AI_Readme.md                      # 本文档
├── ARCHITECTURE.md                   # 架构详细文档
├── 3rdParty/
│   ├── glfw/                         # 窗口
│   ├── glm/                          # 数学
│   ├── imgui/                        # Debug GUI
│   ├── stb/                          # 纹理加载
│   ├── tinyobjloader/                # OBJ 解析
│   └── VulkanMemoryAllocator/        # VMA 显存管理
├── shaders/
│   ├── mrt.vert/.frag                # 几何通道 (MRT)
│   ├── grid_mrt.vert/.frag           # 程序化无限网格
│   ├── skybox_mrt.vert/.frag         # 天空盒
│   ├── lighting.vert/.frag           # 延迟光照合成
│   ├── ssao.frag                     # SSAO
│   ├── fullscreen.vert               # 全屏三角形 VS
│   └── light_cube.vert/.frag         # 光源调试可视化
├── Resources/
│   ├── Models/DamagedHelmet/         # 当前展示模型 (OBJ + PBR贴图)
│   ├── Models/Sponza/glTF/           # Sponza 场景 (glTF, 待接入)
│   └── Texture/                      # HDR 环境贴图 + PBR 贴图集
└── src/
    ├── Core/
    │   ├── Application.h/.cpp        # 引擎总控 (已瘦身)
    │   ├── Descriptor.h/.cpp         # 描述符 Builder/Allocator (旧, 基本不用)
    │   ├── DescriptorManager.h/.cpp  # 新描述符管理器 (bindless 风格)
    │   ├── FrameInfo.h               # 每帧数据总线
    │   ├── ImGuiSystem.h/.cpp        # Debug UI 面板
    │   ├── Material.h                # PBR 材质定义
    │   └── MaterialSystem.h/.cpp     # 材质/纹理缓存管理
    ├── Render/
    │   ├── IRenderPass.h             # 渲染通道抽象接口
    │   ├── RenderPipeline.h          # 通道容器
    │   ├── GeometryPass.h/.cpp       # 几何通道 (管理 RenderPass + GeometrySystem)
    │   ├── GeometrySystem.h/.cpp     # 几何渲染逻辑 (3 个 Pipeline)
    │   ├── LightingPass.h/.cpp       # 光照合成通道
    │   ├── LightingSystem.h/.cpp     # 光照合成逻辑
    │   ├── PostProcessPass.h/.cpp    # 后处理基类
    │   └── SSAOPass.h/.cpp           # SSAO
    ├── RHI/
    │   ├── Device.h/.cpp             # Vulkan 实例/设备封装 + VMA
    │   ├── GBuffer.h/.cpp            # G-Buffer (5 附件)
    │   ├── Pipeline.h/.cpp           # 图形管线封装
    │   ├── Renderer.h/.cpp           # 帧生命周期
    │   ├── Swapchain.h/.cpp          # 交换链 + 深度
    │   └── Texture.h/.cpp            # 纹理 (文件 + 内存加载)
    └── Scene/
        ├── Camera.h/.cpp             # 投影/视图矩阵
        ├── CameraController.h/.cpp   # FPS + Orbit 相机
        ├── GameObject.h              # 游戏对象 (ECS-lite)
        └── Model.h/.cpp              # 顶点/索引缓冲 + OBJ 加载
```

---

## 4. 渲染架构

### 渲染管线（3 个 Pass 顺序执行）

```
1. GeometryPass → GeometrySystem
   ├── geometryPipeline  (mrt.vert/frag)      标准 PBR 物体
   ├── gridPipeline      (grid_mrt.vert/frag)  程序化无限网格
   └── skyboxPipeline    (skybox_mrt.vert/frag) 天空盒
   输出: G-Buffer (Position + Normal + Albedo + PBR + Depth)

2. SSAOPass → PostProcessPass 基类
   输入: G-Buffer Position + Normal
   输出: R8_UNORM 单通道遮蔽贴图

3. LightingPass → LightingSystem
   输入: 全部 G-Buffer + SSAO + 环境贴图
   输出: 交换链最终图像 + ImGui 覆盖
```

### G-Buffer 布局

| 附件 | 格式 | 内容 |
|------|------|------|
| Position | R16G16B16A16_SFLOAT | 世界空间位置 |
| Normal | R16G16B16A16_SFLOAT | 世界空间法线 |
| Albedo | R8G8B8A8_UNORM | 基础颜色 (sRGB 解码后) |
| PBR | R8G8B8A8_UNORM | R=金属度, G=粗糙度 |
| Depth | D32_SFLOAT | 深度 |

### 描述符集设计

```
Set 0 (Global — 所有 Pass 共享, 创建时写一次):
  binding 0: GlobalUbo        (uniform buffer, VERTEX | FRAGMENT)
  binding 1: environmentMap   (combined image sampler, FRAGMENT)
  binding 2: inPosition       (G-Buffer)
  binding 3: inNormal         (G-Buffer)
  binding 4: inAlbedo         (G-Buffer)
  binding 5: inPbr            (G-Buffer)
  binding 6: inSSAO           (SSAO 输出)

Set 1 (Material — 每材质一个, GeometryPass 使用):
  binding 0: albedoMap
  binding 1: normalMap
  binding 2: metallicMap
  binding 3: roughnessMap

Set 1 (SSAO Local — SSAOPass 专用, 不同 layout):
  binding 0: SSAOUbo         (uniform buffer)
  binding 1: texNoise        (4×4 噪声纹理)
```

### 光照模型

- **BRDF**: Cook-Torrance + GGX 法线分布 + Smith 几何遮蔽 + Schlick 菲涅尔
- **方向光**: 可调方向 + 颜色
- **点光源**: 最多 100 个, 球面衰减
- **IBL**: 等距矩形 HDR 环境贴图, 漫反射/镜面反射分拆
- **色调映射**: ACES Filmic
- **Gamma**: 2.2 校正

---

## 5. 设计模式与约定

| 模式 | 位置 | 说明 |
|------|------|------|
| 策略/接口 | `IRenderPass` | 所有 Pass 继承, 实现 `init()`/`execute()`/`onResize()` |
| 组合/管线 | `RenderPipeline` | 持有 `vector<unique_ptr<IRenderPass>>`, 按序执行 |
| Builder | `DescriptorBuilder` | 流式 API 构建描述符集 (旧, 基本不用) |
| 对象池 | `DescriptorManager` | 预分配大池, 管理 Set 0 + Set 1 槽位数组 |
| RAII | 所有 RHI 类 | Vulkan 资源在析构函数中自动释放 |
| 数据总线 | `FrameInfo` | 所有每帧可变数据打包为一个 struct |
| ECS-lite | `GameObject` | Move-only, 自增 ID, 持有 Transform + Model + Material |
| 缓存 | `MaterialSystem` | 纹理和材质缓存, 避免重复加载 |

### 代码约定

- 禁止拷贝 (`= delete`), 允许移动 (`= default`)
- 使用 `std::unique_ptr` 表达独占所有权, `std::shared_ptr` 用于 Model/Material/Texture 等共享资源
- 中文注释在 MSVC 下通过 `/utf-8` 编译选项支持

---

## 6. 核心类 API 速查

### FirstApp (Application.h)
引擎总控。构造时创建一切, `run()` 驱动主循环。

**成员**:
- Window, Device, Swapchain, Renderer, GBuffer
- `DescriptorManager` — 描述符池/Set/布局管理
- `Scene` — 场景图、相机、游戏对象
- `RenderPipeline` — Pass 容器
- `ImGuiSystem` — Debug 面板
- `globalUboBuffer` + `uboMapped` — 全局 UBO (持久映射)
- `environmentTex` — HDR 环境贴图

### DescriptorManager (DescriptorManager.h)
Bindless 风格的描述符管理器。创建时分配一个大池, 内部管理 Set 0 (Global) 和 Set 1 (Material) 槽位数组。

**关键 API**:
```cpp
void init(Device&, uint32_t maxMaterials=256);
VkDescriptorSetLayout getGlobalSetLayout();
VkDescriptorSetLayout getMaterialSetLayout();
void buildGlobalSet(ubo, envInfo, gBuffer, ssaoSampler, ssaoView);  // 调用一次
uint32_t allocateMaterialSet(const Material& material);             // 每个材质调用一次
VkDescriptorSet getMaterialSet(uint32_t index);                     // 渲染时取
```

### MaterialSystem (MaterialSystem.h)
材质和纹理的缓存管理器。

```cpp
void initDefaultTextures(Device&, VkCommandPool);        // 创建 1×1 默认贴图
void buildMaterialDescriptor(shared_ptr<Material>, DescriptorManager&);  // 分配描述符
shared_ptr<Material> getOrCreateMaterial(const string& name);
shared_ptr<Texture> loadTexture(string path, Device&, VkCommandPool);
shared_ptr<Texture> getTexture(const string& path) const;
void cleanup();
```

### Material (Material.h)
PBR 材质。含 `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `alphaMode`, 5 个纹理指针, 以及 `descriptorSetIndex`。静态默认贴图在 `MaterialSystem::initDefaultTextures()` 中填充。

### Scene (Scene.h)
持有 `gameObjects`, `camera`, `cameraController`, `engineSettings`, `telemetry`, `materialSystem`。

### CameraController (CameraController.h)
双模式相机 (FPS + Orbit)。所有参数公开, ImGui 面板直接读写。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `moveSpeed` | 5.0 | 移动速度 |
| `sprintMultiplier` | 3.0 | Shift 冲刺倍率 |
| `lookSensitivity` | 0.15 | 鼠标灵敏度 |
| `smoothEnabled` | true | 阻尼开关 |
| `smoothFactor` | 10.0 | 阻尼系数 |
| FPS 模式 | 右键拖拽 | WASD 移动 |
| Orbit 模式 | 中键拖拽 | 绕焦点旋转, 滚轮调距离 |

### FrameInfo (FrameInfo.h)
每帧数据总线。包含 frameIndex, dt, commandBuffer, settings&, telemetry&, gameObjects&, projectionMatrix, viewMatrix, globalUboBuffer, descriptorManager*, cameraObj*, cameraController*。

### EngineSettings (FrameInfo.h)
运行时参数, ImGui 面板可调: `ssaoEnabled/Radius/Bias/KernelSize`, `ssrEnabled`(预留), `shadowEnabled`(预留), `directionalLightDir/Color`, `exposure`。

---

## 7. 当前状态与待办

### 已完成

- [x] 延迟渲染管线 (Geometry → SSAO → Lighting)
- [x] PBR (Cook-Torrance GGX) + IBL + ACES 色调映射
- [x] 描述符管理重构 (Set 0 / Set 1 拆分, bindless 风格)
- [x] Application 瘦身 (Scene / DescriptorManager 拆出)
- [x] 材质系统 (Material + MaterialSystem + 默认贴图回退)
- [x] VMA 集成 (全部显存分配通过 VMA)
- [x] 双模式相机 (FPS + Orbit, 平滑阻尼, 滚轮调参)
- [x] ImGui 调试面板 (性能, SSAO 开关/参数, 光照, 相机参数)
- [x] 程序化无限网格
- [x] 天空盒 (HDR 球面映射)

### 进行中 / 待完善

- [ ] **glTF 模型加载** — Sponza 场景在 `Resources/Models/Sponza/glTF/`, 需要引入 tinygltf
- [ ] **Model 子网格支持** — 当前 Model 是单网格, Sponza 需要多个 sub-mesh + 不同材质
- [ ] **Material 系统完全集成** — 渲染循环未完全使用 Material, `initMaterials()` 只创建了一个 Helmet 材质
- [ ] **点光源 UBO 大小不匹配** — C++ `GlobalUbo` 有 `pointLights[100]`, shader 中也是 `[100]`, 已修复
- [ ] **SSR Pass** — `EngineSettings::ssrEnabled` 已预留
- [ ] **Shadow Pass** — `EngineSettings::shadowEnabled` 已预留
- [ ] **ImGui 材质面板** — 目前只能选 Helmet 材质, 没有材质切换 UI
- [ ] **窗口可缩放** — 当前硬编码 1920×1080, `onResize()` 接口已定义但未使用

### 已知问题

1. **UBO 字段对齐**: 顶点着色器 (mrt.vert, grid_mrt.vert, skybox_mrt.vert) 中 GlobalUbo 声明为 `vec3 lightDirection` 但 C++ 是 `vec4`, 不过顶点着色器只读 `projectionView`, 暂不影响。
2. **调试光源**: `light_cube` shader 和 `renderDebugLights()` 已实现但当前未启用。
3. **DescriptorBuilder/DescriptorAllocator**: 旧描述符系统仍在 `Descriptor.h/.cpp` 中, 已不再被核心渲染路径使用, 但可用于临时 set 创建。

---

## 8. 扩展指南

### 添加新的后处理 Pass

1. 创建新类继承 `IRenderPass`
2. 实现 `init()`, `execute()`, `onResize()`, `getName()`
3. 可选继承 `PostProcessPass` (如果输出到独立纹理)
4. 在 `Application` 构造中 `renderPipeline.addPass()` 插入到正确位置
5. 在 `ImGuiSystem` 中添加对应的 `renderXxxPanel()` 方法

### 接入 glTF 模型

```
1. 引入 tinygltf (header-only, 放到 3rdParty)
2. Model 添加 sub-mesh 概念 (indexOffset/count + materialIndex)
3. 为每个 glTF 材质创建 Material, 通过 MaterialSystem 加载贴图
4. GeometrySystem::render() 按 sub-mesh 切换 materialSet
```

### 添加新的 ImGui 调试面板

在 `ImGuiSystem` 中添加新的 `renderXxxPanel()` 方法, 遵循现有模式:
```cpp
void renderXxxPanel(EngineSettings& settings) {
    if (!ImGui::CollapsingHeader("Section Name")) return;
    // UI 元素...
}
```

---

## 9. 调试与验证

- **Validation Layers**: Debug 构建自动启用 `VK_LAYER_KHRONOS_validation`
- **ImGui 面板**: 运行时显示 FPS、绘制调用数、三角形/顶点数
- **SSAO 调试**: ImGui 面板上可开关 SSAO 并调节所有参数
- **相机调试**: ImGui 面板上可拖拽位置/旋转、调节所有相机参数

---

## 10. 换电脑继续开发的步骤

1. 修改 `CMakeLists.txt` 中的 Vulkan SDK 路径 (`MY_VULKAN_DIR`) 和 `glslangValidator` 路径
2. 确保 3rdParty 子目录完整 (glfw, glm, imgui, stb, tinyobjloader, VulkanMemoryAllocator)
3. `mkdir build && cd build && cmake .. && cmake --build . --config Debug`
4. 阅读本文档和 `ARCHITECTURE.md`
5. 从第 7 节"待完善"列表中选择任务开始
