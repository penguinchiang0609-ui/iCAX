# RenderData 方案文档

## 设计原则

`RenderData` 是契约层，只定义稳定数据结构。具体渲染路线由实现层决定：

```text
RenderData
    ↓
RenderService
    ↓
PDORenderService / OpenGLRenderService / QtRenderService / WPFRenderService
```

## 多 project / 多 scene

`RenderData` 本身不持有 project 和 scene。多项目隔离由 `RenderService` 负责，数据结构只携带几何、实例、视图和版本信息。

## 非目标

- 不管理 GPU buffer。
- 不保存 renderer pipeline/shader。
- 不持久化项目数据。
- 不直接访问 Repository 或 ResourceLibrary。
