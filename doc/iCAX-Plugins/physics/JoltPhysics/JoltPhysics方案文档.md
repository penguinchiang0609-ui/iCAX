# JoltPhysics 方案文档

## 1. 源码位置

```text
src/iCAX-Plugins/physics/JoltPhysics/
```

## 2. 文件结构

```text
JoltPhysics/
  JoltPhysicsTypes.h
  JoltPhysicsScene.h / JoltPhysicsScene.cpp
  JoltPhysicsSceneCatalog.h / JoltPhysicsSceneCatalog.cpp
  JoltPhysics.h
```

## 3. 依赖方向

```text
Product / Behaviour / Product Service
  -> JoltPhysics
    -> Jolt SDK

iCAX-Engine/framework
  不依赖 JoltPhysics
```

物理插件是可替换插件。后续如果改用 PhysX、Bullet 或自研碰撞库，应新增插件或替换产品层绑定，不修改 framework。

## 4. 运行关系

物理不单独成为一端：

```text
Project worker thread
  -> Behaviour.Tick(...)
    -> sync Repository/Resource to physics scene
    -> scene.Step(...)
    -> scene.Raycast(...)
    -> update backend state or write PDO result
```

这样做的原因是物理查询依赖 Repository、Resource、当前命令和产品上下文。把物理拆成独立端点会引入额外同步协议，并让同一进程内的数据又绕回 mailbox/PDO。

## 5. 多场景

`CJoltPhysicsSceneCatalog` 是场景目录，产品层可以按 viewport 或业务场景分配 scene ID：

- `MainViewport`：主三维窗口。
- `PreviewViewport`：导入预览。
- `SimulationViewport`：加工仿真。
- `MeasureScene`：测量或临时拾取场景。

每个 scene 拥有自己的 Jolt `PhysicsSystem`。某个 scene 的异常或数据错误不应污染其他 scene。

## 6. 数据同步策略

推荐由 Behaviour 或产品服务负责同步：

```text
Repository/Resource changed
  -> mark physics sync dirty

Behaviour.Tick
  -> rebuild/create/update body/collider
  -> process pointer/raycast request
  -> publish result
```

Body 的 `nOwnerObjectID` 用来回到产品对象、Entity 或显示对象。物理插件不关心这个 ID 的语义。

## 7. 与 RenderPDO 的配合

显示数据和碰撞数据可以来自同一份 mesh/resource，但它们是两个目的：

- `RenderPDO`：告诉前方画什么。
- `JoltPhysics`：告诉后端如何碰撞、拾取和查询。

前端使用 H5、WPF 或 QT 都不影响 JoltPhysics。前端只需要按契约读写 PDO 或发送 mail。

## 8. Jolt SDK 接入

插件项目默认不参与整解构建。需要构建时准备官方 Jolt Physics SDK：

```text
.deps/jolt/JoltPhysics/
```

或设置环境变量：

```text
ICAX_JOLT_ROOT=<JoltPhysics repo root>
ICAX_JOLT_LIB_DIR=<directory containing Jolt.lib>
```

`JoltPhysics.vcxproj` 会检查 `Jolt/Jolt.h` 和 `Jolt.lib`。缺失时独立构建会报出明确错误。

Jolt 官方 CMake 默认使用静态 CRT。iCAX DLL 工程使用动态 CRT，因此本地构建 Jolt 时应指定：

```text
cmake -S .deps/jolt/JoltPhysics/Build -B .deps/jolt/build_vs2022_x64 -G "Visual Studio 17 2022" -A x64 -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_PERFORMANCE_TEST=OFF -DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF
cmake --build .deps/jolt/build_vs2022_x64 --config Release --target Jolt
```

`JoltPhysics.vcxproj` 中的 `JPH_*` 编译宏必须和生成 `Jolt.lib` 时的 CMake 选项一致。Jolt 会在 `RegisterTypes()` 阶段校验版本位，宏不一致会直接中止进程。

## 9. 单元测试

测试工程位置：

```text
src/tests/icax-plugins/physics/JoltPhysics/JoltPhysicsTest/
```

测试工程依赖 Jolt SDK 和 JoltPhysics 插件，默认不参与整解构建。当前覆盖：

- scene catalog 多场景独立管理。
- box raycast 命中 owner object 和表面法线。
- triangle mesh raycast。
- 非法 triangle mesh 输入校验。

## 10. 后续扩展

- 增加 capsule、convex hull 等 collider。
- 增加 broadphase query、shape cast 和 overlap query。
- 增加基于项目数据的 collider rebuild helper。
- 增加 `interaction.pointer` / `interaction.hit_result` PDO 插件。
