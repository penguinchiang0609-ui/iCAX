# ColliderService 方案文档

## 依赖关系

```text
Product / Behaviour
    ↓
IColliderService
    ↓
JoltColliderService / 其他实现
```

接口依赖 `ColliderData` 和 framework `Services`，不依赖 Jolt。

## 生命周期

产品或项目启动时创建 scene。项目关闭时调用 `DestroyProject(ProjectID)`。服务卸载时清理全部 project 数据。

## 与 Behaviour 的关系

Behaviour 负责观察 Repository/Resource 版本变化，生成或更新 `ColliderData`，再调用 `IColliderService` 更新碰撞场景。
