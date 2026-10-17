# Sentinel 依赖与验证工具

## 一条命令

从仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1
```

默认行为：

1. 运行 `tools/SetupDependencies.ps1`，检查/下载固定版本的第三方依赖；
2. 执行 TD-08 dead-file guard（废弃 shader / Descriptor 旧系统 / 源目录 `.spv` 不得回归）；
3. 删除旧 `build/`，执行全新 configure + Debug build；
4. 跑 `--jobtest`；
5. 跑 `--config-test`（TD-09 配置 v0->v2 迁移、备份、损坏/未来版本保护）；
6. 跑四种 `--selftest 120` 组合（Bloom / SSAO 开关）；
7. 跑 `--selftest 120 --cross-queue-test`（TD-02 跨队列回归，至少 20 帧）；
8. 跑 `--selftest 20 --compute-test`（TD-07 compute probe 独立回归）；
9. 跑 `--selftest 120 --resize-test`（TD-03 窗口缩放 / 动态分辨率 / 最小化恢复）；
10. 跑 `--selftest 120 --render-scale 0.5`（TD-03 启动即降分辨率）；
11. 跑 `--selftest 150 --material-stress`（TD-04 材质槽位延迟回收压力测试）；
12. 跑 `--selftest 20 --subresource-test`（TD-05 mip/layer + buffer 区间正向探针）；
13. 跑 `--subresource-error-test`（TD-05 写 mip0 后读 mip1 的反例检查）；
14. 每次运行都要求退出码为 0、stderr 字节数为 0。

任意一步失败都会返回非零退出码。日志在 `build/validation/<timestamp>/`。

Bloom 关闭的 selftest 还会检查 `[TD06] Bloom disabled: skipping Bright + Down + Up passes`；
普通 selftest 会检查 `[TD07] ComputeProbe disabled: no probe dispatches or CPU readback`，
只有 `--compute-test` 才允许出现 D-5 probe 的 GPU 写/CPU 读回。

## Vulkan SDK

CMake 通过 `find_package(Vulkan REQUIRED)` 自动定位。推荐设置：

```powershell
$env:VULKAN_SDK = "D:\work\vulkan"
```

也可以显式传：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1 -VulkanSdk "D:\work\vulkan"
```

## 快速迭代

依赖已经准备好、只想增量构建时：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Validate.ps1 -Incremental -SkipSetup -Frames 20
```

## 固定依赖版本

TD-03 的动态分辨率也可以在运行时通过 ImGui 的 `Render Scale` 滑杆调整，
或使用命令行 `--selftest N --render-scale 0.5`。

`SetupDependencies.ps1` 的 manifest 固定了下面这些版本/提交：

| 依赖 | 版本 |
|---|---|
| GLFW | 3.4 |
| GLM | 1.0.3 |
| Dear ImGui | 1.92.6 |
| stb | `f4a71b1` |
| tinyobjloader | v2.0.0 |
| VulkanMemoryAllocator | v3.4.0 |
| tinygltf | `b3e0d78` |
| EnTT | v3.16.0（C++17 兼容） |

`3rdParty/` 不入库，下载缓存放在 `3rdParty/.cache/`。可以用
`-DependenciesRoot <path>` 指向别的目录。`spdlog` 是可选依赖，当前引擎未启用。

## CMake 约定

- `SOURCES` 使用 `CONFIGURE_DEPENDS`，新增 `src/*.cpp` 后不再需要手动重新 configure。
- 所有必需头文件在 configure 阶段检查，缺依赖时会直接提示运行本工具。
- Shader 仍由 Vulkan SDK 的 `glslangValidator` 在构建前编译。