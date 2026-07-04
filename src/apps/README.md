# apps

`apps` 是具体产品目录。每个产品都是独立交付单元，通过 manifest 关联自己的 C++ backend 插件、H5 webpage 入口和项目文件 magic。

## 目录结构

- `laser-3d-cam/`：三维线条切割 CAM，首版覆盖模型拓扑拾取和刀路列表。

## 产品目录约定

正式产品建议包含：

- `product.manifest.json`：产品定义入口，同时声明 backend 和 webpage。
- `webpage/`：产品 H5 页面、面板和组件。
- `resources/`：产品内置资源和资源说明。

公共 H5 前端框架位于 `src/iCAX-UI/`。具体产品的 backend 扩展放在 `src/iCAX-Plugins/`，具体产品的前端代码位于 `src/apps/<product-id>/webpage/`。
