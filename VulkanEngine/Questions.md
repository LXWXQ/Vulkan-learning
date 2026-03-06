# 2026 渲染程序员（Vulkan/跨平台/引擎方向）面试与自研知识清单

## 一、 Vulkan 核心架构（底层掌控力）
> 目标：证明你不仅会调用 API，更理解 GPU 驱动的行为与硬件调度。

### 1. 同步机制 (Synchronization) - 渲染性能的命门
* **Pipeline Barriers：** 深度理解 Execution Barrier 与 Memory Barrier 的区别；掌握 srcStage/dstStage 与 Access Masks 的对应关系。
* **Semaphores vs. Fences：** 前者用于 GPU-GPU 同步（如 Image Acquisition -> Rendering -> Present），后者用于 GPU-CPU 同步（如等待帧录制完成）。
* **多线程录制：** * **Command Pool 策略：** 为什么每个线程需要独立的 Pool？如何利用 `RESET_COMMAND_POOL_BIT` 优化内存。
    * **Secondary Command Buffers：** 在复杂场景中如何并行录制指令以榨干多核 CPU。

### 2. 内存与资源管理 (Memory & Resource)
* **VMA (Vulkan Memory Allocator)：** 理解大块申请（Sub-allocation）的原理，减少驱动层面的内存映射开销。
* **内存对齐与布局：** 解决 std140/std430 问题，理解 `alignas(16)` 的必要性及其对 Cache Line 的影响。
* **Descriptor Management：**
    * **Frequency-based Sets：** 按更新频率（全局/材质/物体）分组。
    * **Bindless Rendering：** 掌握 `Descriptor Indexing`，理解现代引擎如何摆脱传统绑定限制（UE5 核心思想）。
* **Push Constants：** 适用场景（通常 < 128 字节）及其对比 UBO 的延迟优势。

### 3. 管线与状态 (Pipeline & PSO)
* **PSO 优化：** 解决 Shader 编译卡顿，实现 Pipeline Cache 的序列化与加载。
* **State Sorting：** 如何在渲染提交前对状态进行排序，最小化上下文切换开销。

---

## 二、 PBR 与光照物理（数学与视觉）
> 目标：发挥硕士背景优势，展现对物理规律的公式拆解与实现能力。

### 1. 渲染方程 (The Rendering Equation)
* **手写并解释：** $$L_o(p, \omega_o) = L_e(p, \omega_o) + \int_{\Omega} f_r(p, \omega_i, \omega_o) L_i(p, \omega_i) (n \cdot \omega_i) d\omega_i$$

### 2. Cook-Torrance BRDF 拆解
* **D (NDF)：** Trowbridge-Reitz GGX 的数学意义与长尾效应。
* **G (Geometry)：** Smith 遮蔽函数的物理依据（微表面自遮挡）。
* **F (Fresnel)：** Schlick 逼近公式，以及金属 $F_0$ 为何直接使用 Albedo。

### 3. IBL 与环境光
* **预卷积 (Pre-convolution)：** 离线生成漫反射球谐函数（SH）与镜面反射预过滤图。
* **Split Sum Approximation：** 深刻理解 Epic Games 提出的双重求和近似原理及其对实时性的贡献。

### 4. HDR 与后处理
* **Tone Mapping：** 对比 Reinhard 与 ACES 的视觉差异（高光去饱和、对比度保留）。
* **Color Grading：** LUT 的原理与应用。

---

## 三、 跨平台工程与引擎架构（实战进阶）
> 目标：证明你具备构建“工业级”渲染引擎的思维。

### 1. RHI (Rendering Hardware Interface) 设计
* **API 抽象：** 如何设计一套通用接口同时兼容 Vulkan、DX12 和 Metal。
* **Shader Cross-compilation：** 利用 `glslang`/`Slang` 配合 `SPIRV-Cross` 实现一份 Shader 多端运行。

### 2. 现代渲染框架
* **帧图 (Frame Graph / Render Graph)：** 利用图论背景，自动处理资源生命周期、自动插入 Barrier 和 Aliasing 优化。
* **Asset Pipeline：** 掌握 ASTC/ETC2/BC7 等纹理压缩格式及其在跨平台工程中的选择建议。

### 3. 移动端优化 (Tile-based Rendering)
* **TBR/TBDR 架构：** 理解 Load/Store Op 如何配合 Transient Attachments 节省带宽。
* **Subpass 优势：** 解释在移动端 GPU 上不切出 Tile Memory 直接进行多通路计算的巨大价值。

---

## 四、 现代管线与性能调优
> 目标：解决大场景、多光源下的实际性能瓶颈。

### 1. 延迟渲染与光源处理
* **G-Buffer 压缩：** 法线八面体编码等压缩技术以节省带宽。
* **光源剔除：** Tiled 与 Clustered Deferred 的区别，如何高效处理 100+ 动态光源。

### 2. 阴影与遮挡剔除
* **软阴影：** PCF 与 PCSS 的实现差异。
* **CSM：** 级联阴影贴图的接缝处理与稳定算法。
* **遮挡剔除：** GPU Driven Pipeline (Draw Indirect)、HZB (Hierarchical Z-Buffer) 剔除。

### 3. 性能分析工具
* **RenderDoc：** 定位资源状态错误、内存泄漏。
* **NSight / Xcode Instruments：** 分析 GPU 耗时瓶颈、带宽压力与热点指令。

---

## 五、 计算机科学与数学
> 目标：展示扎实的基础功底与代码质量。

### 1. 现代 C++ (C++17/20)
* **内存安全：** RAII 资源管理，避免在渲染循环中使用 `shared_ptr` 的引用计数开销。
* **性能编程：** 数据导向设计 (DOD/ECS) 以提升 Cache 命中率；SIMD (SSE/NEON) 加速矩阵运算。

### 2. 数学功底
* **变换矩阵：** 投影矩阵推导，Vulkan Z 轴（0 到 1）与 OpenGL 的差异处理。
* **四元数 (Quaternions)：** 解决万向锁问题的原理及 Slerp 插值。

### 3. 空间数据结构
* **BVH (Bounding Volume Hierarchy)：** 加速结构在剔除与光线追踪中的应用。
* **Scene Graph：** 利用树状结构管理物体依赖与空间裁剪。