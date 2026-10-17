# Sentinel 未来功能计划

> 本文档只放**尚未开始的主功能**，以及与这些主功能强相关的扩展能力。
> 当前代码里需要优先修的技术债在 `TECH_DEBT.md`，不要混到一起。
>
> 当前基础：D-1 到 D-7 已完成，包括渲染图 / HDR+Bloom / bindless / 2 帧在飞 /
> 异步 compute / Job System / EnTT ECS + Transform 层级 + 空间索引。
> 现在还缺的是真实世界内容和 GPU 驱动渲染能力。

## 1. 功能总览与顺序

| 阶段 | 主功能 | 目标 | 前置条件 |
|---|---|---|---|
| F1 | GPU 驱动渲染 | GPU culling、indirect draw、实例化、GPU 场景数据 | TD-02、TD-05 |
| F2 | 地形系统 | 高度场 chunk、LOD、材质混合、流送、碰撞数据 | F1、JobSystem 生产接入 |
| F3 | 体素系统 | 体素 chunk、网格生成、LOD、编辑、碰撞 | F1、F2 的 chunk 框架 |
| F4 | 资产与场景 | glTF node-instance 层级、材质流送、Prefab/场景描述 | 资产管线重构 |
| F5 | 运行时架构 | JobSystem 任务图、ECS systems、SpatialIndex 增量更新 | F2/F3 的压力测试 |
| F6 | 渲染特性 | 阴影、SSR、自动曝光、compute Bloom、时间超分 | F1/F2 的渲染基础 |
| F7 | 物理 / 动画 / 工具 | Jolt、ozz、Tracy、编辑器 | 世界数据与坐标系统稳定 |

> 推荐先从 F1 开始，不要直接上地形。GPU 驱动渲染是地形 chunk 数量上来后的性能前提。
---

## 2. F1：GPU 驱动渲染

### 目标

- GPU 视锥剔除 / Hi-Z 遮挡剔除。
- indirect draw / indirect dispatch。
- Transform / bounds / material 等场景数据走 SSBO 或 GPU-driven buffer。
- 实例化：减少相同模型/材质的 draw call。
- 为地形和体素 chunk 提供统一的批量提交路径。

### 关键设计

```text
CPU: 场景实体/Transform -> GPU Scene Buffer（每帧或 dirty 时更新）
GPU compute: 视锥剔除 + LOD 选择 -> indirect argument buffer
Graphics: vkCmdDrawIndexedIndirect / 实例化 draw
```

### 前置技术债

- TD-02：compute 写 indirect buffer，graphics 消费，必须跨队列同步。
- TD-05：如果引入 mip/layer 独立资源，需要子资源语义校验。

### 验收标准

- 104 个子网格的 Sponza 在不增加 draw call 的情况下支持实例化/间接提交。
- GPU culling 后 CPU 不再逐个遍历所有实体决定是否绘制。
- 验证层 0 字节，CPU `GeometrySystem::render` 耗时显著下降。
---

## 3. F2：地形系统

### 目标

- 高度场地形 chunk（例如 64x64 / 128x128 顶点）。
- chunk LOD / 邻居缝合，避免 T 形裂缝。
- 多层材质混合（albedo / normal / roughness / height）。
- 地形流送：按相机距离异步加载/卸载 chunk。
- 地形碰撞数据生成，供物理系统消费。
- 地形编辑 API（后续编辑器使用）。

### CPU/GPU 分工

- CPU JobSystem：高度采样、chunk 网格生成、碰撞数据准备。
- GPU compute：视锥剔除、LOD 选择、indirect draw 参数生成。
- 渲染侧复用 F1 的 GPU-driven 提交。

### 需要补的运行时能力

- SpatialIndex 从每帧重建升级为 chunk/区域增量更新。
- JobSystem 支持任务依赖图或至少分层 parallelFor（chunk 生成 -> LOD -> 上传）。
- 动态分辨率/窗口缩放前先修 TD-03。

### 验收标准

- 相机移动跨越多个 chunk 时无明显卡顿和裂缝。
- chunk LOD 切换可见，验证层 0 字节。
- 地形总顶点量增加时 FPS 不线性崩塌。
---

## 4. F3：体素系统

### 目标

- 固定大小体素 chunk（例如 32^3）。
- marching cubes / surface nets 网格生成。
- chunk LOD + 邻接缝合。
- 体素编辑：放置/删除方块，局部重建。
- 体素/地形混合的碰撞数据。

### 依赖

- F1 的 GPU 剔除 / indirect draw。
- JobSystem 并行 chunk meshing。
- 若支持动态编辑，材质/描述符槽位回收需先修 TD-04。
---

## 5. F4：资产与场景管线

### 目标

- 保留 glTF 内部 node 层级：不再把整个模型几何永久烘焙成世界空间。
- 每个 glTF primitive instance 可独立变换，支持门、机械部件、植被等局部动态。
- 资产引用计数 + 异步加载 + 材质流送。
- Prefab / 场景描述格式（未来编辑器读写）。

### 当前阻塞

D-7 已有 entity 级 Transform 层级，但 `GLTFLoader.cpp` 仍然把 node 变换直接烘焙进
vertex position，导致 Sponza 内部节点不可单独驱动。这是 F4 的首要重构点。

### 验收标准

- 载入 glTF 后可以挑选任意 node 修改 local transform 并实时看到结果。
- 同一份 mesh 可被多个 entity 实例化，不重复占显存。
- 场景反复加载/卸载不泄漏材质槽位和 GPU 资源。
---

## 6. F5：运行时架构

### 目标

- JobSystem：work stealing、任务依赖图、取消、优先级。
- ECS systems：明确的 update 阶段（输入、变换、空间索引、剔除、渲染提交）。
- SpatialIndex：dirty flag / 增量更新 / 分块索引 / 大规模场景查询。
- 场景序列化与运行时热重载。

### 注意

这些是 F2/F3 的规模化前置。当前版本能工作，但不要在地形 chunk 数量上来后
仍保留每帧重建全部空间索引和全局单队列的实现。
---

## 7. F6：渲染特性

| 功能 | 说明 | 依赖 |
|---|---|---|
| 阴影 | CSM / 局部阴影，接入 deferred lighting | GPU scene buffer |
| SSR | 屏幕空间反射，读取 HDR + depth | HDR 管线已就绪 |
| 自动曝光 | compute 直方图 + 曝光纹理 | compute/graphics 跨队列同步 |
| Compute Bloom | 将 Bloom 金字塔迁移到 compute | TD-06、TD-07 |
| 时间超分 / 动态分辨率 | 内部低分辨率渲染 + 上采样 | TD-03、TAA |
| 颜色分级 / 暗角 / 胶片颗粒 | Present 前插入后效链 | PresentPass 已有 |
---

## 8. F7：物理 / 动画 / 工具

| 方向 | 计划库/方案 | 说明 |
|---|---|---|
| 物理 | Jolt Physics | 地形/体素碰撞、角色控制器、刚体 |
| 动画 | ozz-animation | 骨骼动画、蒙皮 |
| 网格优化 | meshoptimizer | LOD、简化、顶点缓存优化 |
| Profiling | Tracy | 帧级 GPU/CPU 时间线 |
| 日志 | spdlog | 替换散落的 std::cout / std::cerr |
| 编辑器 | 自研 / ImGui + 场景序列化 | 地形、体素、材质、Prefab 编辑 |
---

## 9. 推荐执行顺序

```text
TD-01 (可复现构建/验证)
  -> TD-02 (compute/graphics 跨队列同步)
  -> F1 (GPU-driven rendering / culling / indirect)
  -> F2 (地形 chunk + LOD + 流送)
  -> F3 (体素 chunk)
  -> F4 (资产管线 / glTF node-instance)
  -> F6 (阴影 / SSR / 自动曝光)
  -> F7 (物理 / 动画 / 工具)
```

`F5` 是贯穿 F2/F3 的运行时升级，不是独立终点。
`TD-03` 应在动态分辨率/编辑器之前解决。

---

## 10. 未来计划与当前技术债的边界

- 本文件中的功能允许先设计、后实现；
- 当前已经影响正确性、构建、验证、帧资源生命周期的内容，不放进本文件，统一进
  `TECH_DEBT.md`；
- 当一个未来功能开始实施时，如果暴露出新的现有系统问题，先补到 `TECH_DEBT.md`，
  再继续推进功能。