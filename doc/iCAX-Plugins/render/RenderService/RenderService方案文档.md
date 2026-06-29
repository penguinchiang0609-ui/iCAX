# RenderService 方案文档

## 依赖关系

```text
Product / Behaviour
    ↓
IRenderService
    ↓
具体实现
```

`IRenderService` 依赖 `RenderData` 和 framework `Services`，不依赖 PDO。

## 生命周期

产品或项目启动时创建 scene；项目关闭时调用 `DestroyProject(ProjectID)` 清理该项目的全部渲染现场。服务卸载时应清空全部 project 数据。

## 实现扩展

- H5 前端路线：使用 `PDORenderService`。
- 原生渲染路线：可以实现 `OpenGLRenderService`、`QtRenderService`、`WPFRenderService`。

所有实现都应遵守 `ProjectID + SceneID` 隔离边界。
