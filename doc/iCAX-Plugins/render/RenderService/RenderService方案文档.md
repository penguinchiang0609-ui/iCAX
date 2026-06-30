# RenderService 方案文档

## 依赖关系

```text
Product / Behaviour
    ↓
IRenderService
    ↓ Update(application, product, project, frame time)
    ↓
具体实现
```

`IRenderService` 依赖 `RenderData` 和 framework `Services`，不依赖 PDO。帧入口显式接收 Application/Product/Project 三层 context，使具体实现可以从项目现场取资源、PDO 或服务，但接口本身不绑定任何具体前端路线。

## 生命周期

产品或项目启动时创建 scene；项目关闭时调用 `DestroyProject(ProjectID)` 清理该项目的全部渲染现场。服务卸载时应清空全部 project 数据。

每帧调度顺序建议放在项目线程内：

```text
Project PreSwapPDO
    -> 邮件/命令处理
    -> Universe Tick
    -> RenderService.Update
Project PostSwapPDO
```

如果渲染输出依赖 Behaviour 更新后的实例矩阵或相机状态，应把 `Update` 放在 Universe Tick 之后。

## 实现扩展

- H5 前端路线：使用 `PDORenderService`。
- 原生渲染路线：可以实现 `OpenGLRenderService`、`QtRenderService`、`WPFRenderService`。

所有实现都应遵守 `ProjectID + SceneID` 隔离边界。
