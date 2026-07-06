# JoltPhysics

`JoltPhysics` 是 iCAX 的 Jolt Physics 插件。

它提供 project/scene 级物理世界管理能力。一个 `CJoltPhysicsScene` 对应一个独立碰撞/刚体世界；多个显示场景应创建多个 scene，彼此的 body、collider、raycast 和 simulation step 不共享状态。

## 边界

- 物理引擎在 backend 内部运行，不通过 PDO 和主程序对接。
- PDO 只用于 UI 边界，如 `render.camera`、`interaction.pointer`、`interaction.hit_result`。
- Jolt 不进入 `iCAX-Engine/framework`，只作为可选 plugin。
- 当前提供 box、sphere 和 triangle mesh body，先覆盖刚体、模型拾取和静态碰撞的基础场景。

## Jolt SDK

工程默认不参与主解决方案构建。需要构建本插件时，准备 Jolt Physics SDK，并设置：

```text
ICAX_JOLT_ROOT=<JoltPhysics repo root>
ICAX_JOLT_LIB_DIR=<Jolt lib directory>
```

如果 Jolt 放在仓库根目录 `.deps/jolt/JoltPhysics`，工程会优先尝试该路径。

使用 CMake 构建 Jolt 静态库时，需要和 iCAX DLL 工程保持动态 CRT：

```text
cmake -S .deps/jolt/JoltPhysics/Build -B .deps/jolt/build_vs2022_x64 -G "Visual Studio 17 2022" -A x64 -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF
cmake --build .deps/jolt/build_vs2022_x64 --config Release --target Jolt
```

如果调整 Jolt CMake 选项，需要同步检查 `JoltPhysics.vcxproj` 中的 `JPH_*` 编译宏，避免 `RegisterTypes()` 版本位校验失败。
