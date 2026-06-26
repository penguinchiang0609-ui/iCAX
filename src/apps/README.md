# apps

`apps` 是具体产品目录。每个产品都是独立交付单元，同时包含产品定义、C++ backend 扩展、H5 webpage 扩展和产品协议。

## 目录结构

- `flat-laser-cam/`：平面激光 CAM 产品。
- `sample/`：旧示例应用，后续只作为实验参考。
- `cax-host/`：旧 CAXHost 宿主应用。
- `mfc-sample/`：旧 MFC 示例应用。

## 产品目录约定

正式产品建议包含：

- `product.manifest.json`：产品定义入口，同时声明 backend 和 webpage。
- `backend/`：产品 C++ 扩展模块。
- `webpage/`：产品 H5 页面、面板和组件。
- `protocol/`：产品自己的命令、事件、PDO 和文件格式约定。
- `resources/`：产品内置资源和资源说明。

公共 H5 前端框架位于 `src/iCAX-UI/`。具体产品的前端代码位于 `src/apps/<product-id>/webpage/`。
