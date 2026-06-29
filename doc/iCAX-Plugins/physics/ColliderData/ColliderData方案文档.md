# ColliderData 方案文档

## 设计原则

`ColliderData` 只保存碰撞数据契约，不包含场景管理、物理求解或拾取逻辑。

## 与几何/渲染关系

复杂 BRep、NURBS、B-spline 通常需要生成派生 collider：

```text
真实几何
    ↓
RenderData
    ↓
ColliderData
```

Render mesh 和 collision mesh 可以同源，但不要求是同一份数据。
