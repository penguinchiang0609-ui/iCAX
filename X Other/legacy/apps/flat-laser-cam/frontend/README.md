# Flat Laser CAM Frontend

本目录承载平面激光 CAM 的 H5 前端产品模块。

前端模块由 ApplicationHost 在用户选择产品后按产品定义加载。当前目录只声明产品自己的 UI 入口和前端侧产品 manifest，不向 framework 写入具体产品信息。

## 目录结构

- `entry.mjs`：产品前端入口。
- `productManifest.mjs`：前端开发态使用的产品 manifest。
- `pages/`：产品级页面。
- `panels/`：产品级侧栏或工具面板。
- `components/`：产品前端复用组件。

