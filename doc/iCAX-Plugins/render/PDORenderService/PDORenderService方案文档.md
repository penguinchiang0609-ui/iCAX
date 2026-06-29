# PDORenderService 方案文档

## 内部结构

```text
CPDORenderService
    ProjectID
        RenderSceneID
            Meshes
            Polylines
            Toolpaths
            Instances
            Views
```

服务可以是 application 级单例，但所有数据都按 `ProjectID + SceneID` 隔离。

## 序列化边界

`RenderData` 是中立结构，`RenderPDO` 是二进制通信 layout。`PDORenderService` 负责两者之间的转换和校验。

当前写出内容包括：

- `Mesh`：点、法向、顶点色、索引。
- `Polyline`：点数组和分段 range。
- `Toolpath`：刀位点、刀轴、进给、刀路 span。
- `InstanceList`：对象实例、几何引用、显示类别、样式引用和变换矩阵。
- `ViewState`：viewport 尺寸、相机语义和矩阵。

序列化时先构造局部 header，再追加数组 payload，最后把 header 拷贝回 payload 起始位置。这样可以避免 `std::vector` 扩容导致内部指针失效。

## 关闭清理

项目关闭时产品层调用 `DestroyProject(ProjectID)`。服务卸载时 `OnUnload()` 清空所有项目数据。
