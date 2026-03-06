# 现代渲染引擎重构设计方案 (Modern Rendering Engine Architecture)

## 1. 核心设计理念 (Core Philosophies)
* **数据驱动 (Data-Driven)**：渲染状态和场景数据分离，避免面向对象带来的深层继承树。
* **解耦 (Decoupling)**：UI（ImGui）只做视图，不管理数据；渲染前端（场景组装）与渲染后端（GPU命令提交）分离。
* **状态最小化 (State Minimization)**：通过材质系统和渲染队列，最小化管线状态对象（PSO）的切换开销。

## 2. 整体架构分层 (Architecture Layers)

### 2.1 硬件抽象层 (RHI - Render Hardware Interface)
封装底层的 Vulkan API，向上层提供统一的接口。
* `Device` / `CommandList` / `Buffer` / `Texture`
* **Pipeline Cache System (管线缓存池)**：通过 Hash 算法缓存创建好的 Pipeline State Objects (PSO)。遇到相同 Shader 和渲染状态的请求时，直接返回已有 Pipeline，避免高昂的创建开销。

### 2.2 渲染后端：Render Graph (帧图系统)

这是整个引擎调度的核心，负责取代硬编码的 Pass 列表。
* **Pass Node (通道节点)**：定义每个 Pass（如 Geometry, Lighting, Post-Process）。每个节点只声明自己的 **Inputs**（依赖的纹理/缓冲）和 **Outputs**（输出的纹理/缓冲），以及一个执行具体渲染命令的 Lambda 表达式。
* **Resource Aliasing (资源复用)**：Render Graph 负责在编译期分析生命周期，让不重叠的资源（如已用完的 G-Buffer 和后处理用的中间 Buffer）共享同一块 GPU 内存。
* **Barrier Generator (屏障生成器)**：根据资源的读写依赖，自动推导并插入正确的 Image Layout Transitions 和 Memory Barriers。

### 2.3 渲染前端：材质与场景系统 (Frontend & Scene)

* **ECS (实体组件系统)**：场景由 Entity 组成，挂载不同的 Component。
  * `TransformComponent`：位置、旋转、缩放。
  * `MeshComponent`：顶点和索引数据。
  * `LightComponent`：光源属性。
* **Material System (材质系统)**：
  * **Shader 绑定**：持有特定效果的 Shader。
  * **参数封装**：管理 Descriptor Sets / Uniform Buffers（如漫反射贴图、粗糙度参数）。
  * 多个 Entity 可以共享同一个 Material 实例。
* **Render Queue (渲染队列)**：每帧遍历 ECS 收集所有需要渲染的实体，根据 Material 的 Hash 值进行排序（Sort），合并 Draw Call。

### 2.4 视图与工具层 (View & Tools)
* **ImGui 调试视图**：作为独立模块，每帧读取 ECS 数据（Read-Only 或受控 Write）来绘制 Scene Tree 和 Inspector。勾选“延迟渲染”本质上是通知 Render Graph 切换到另一套预设的拓扑结构。
* **Profiler (性能分析器)**：
  * CPU 端：高精度计时器。
  * GPU 端：封装底层的时间戳查询（Timestamp Queries）和管线统计（Pipeline Statistics），将真实 GPU 耗时、Draw Call 数量、生成图元数反馈给 ImGui 面板。

---

## 3. 核心模块执行流 (Execution Flow)

1. **逻辑更新 (Logic Tick)**：物理系统、动画系统更新 ECS 中的 `TransformComponent` 等数据。
2. **场景收集 (Cull & Collect)**：遍历 ECS，进行视锥体剔除（Frustum Culling），将可见物体的渲染数据压入 `RenderQueue`，并按 Material 排序。
3. **图构建 (Graph Build)**：根据当前的 UI 配置（例如是否开启阴影、AO），构建本帧的 Render Graph 拓扑。
4. **图编译 (Graph Compile)**：Render Graph 分配物理显存，插入管线屏障。
5. **图执行 (Graph Execute)**：遍历图节点，绑定 Pipeline 和 Descriptor Sets，录制 Command Buffer 并提交给 GPU 队列。
6. **UI 绘制 (UI Render)**：最后录制 ImGui 的 Draw Data，提交上屏。

## 4. 目录结构规划建议

```text
Engine/
├── Core/             # ECS, 内存池, 日志, 数学库
├── RHI/              # Vulkan 封装, PSO缓存, 描述符分配器
├── RenderGraph/      # 帧图构建器, Pass节点定义, 资源依赖追踪
├── Renderer/         # 材质系统, 渲染队列, 具体效果的实现 (DeferredRenderer等)
├── Scene/            # 场景图管理, 摄像机控制器
└── Editor/           # ImGui 封装, 性能分析面板, 场景树视图