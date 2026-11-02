# apps

`apps` 是具体产品目录。每个产品都是独立交付单元，通过 manifest 关联自己的 C++ backend 插件、H5 webpage 入口和项目文件 magic。

## 目录结构

- `tube-designer/`：当前公开的管材产品，统一承载设计、制造拆单、排样和加工 UI。
- `five-axis-cam/`：三维五轴 CAM 的六区工作台 UI 首版；复用共享渲染与 CAM 准备能力，完整五轴规划与验证尚未接入。
- `_shared/`：可由各产品显式接入的工作台和 CAD/CAM 能力。

产品框架仍支持同时加载多个产品，各产品使用独立项目、场景和服务注册。共享 CAD、CAM 与机床能力继续位于插件和 `_shared`，不依赖已撤下的产品入口。

## 产品目录约定

正式产品建议包含：

- `product.manifest.json`：产品定义入口，同时声明 backend 和 webpage。
- `webpage/`：产品 H5 页面、面板和组件。
- `resources/`：产品内置资源和资源说明。

公共 H5 前端框架位于 `src/iCAX-UI/`。具体产品的 backend 扩展放在 `src/iCAX-Plugins/`，具体产品的前端代码位于 `src/apps/<product-id>/webpage/`。
