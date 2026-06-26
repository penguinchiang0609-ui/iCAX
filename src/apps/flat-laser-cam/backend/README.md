# Flat Laser CAM Backend

本目录承载平面激光 CAM 的 C++ 后端产品模块。

## 目录结构

- `product/`：产品定义，包含产品 ID、magic number、入口文件和模块列表。
- `component/`：产品数据组件，描述项目中的 EC 数据。
- `behaviour/`：产品行为模块，绑定组件生命周期和业务反应。
- `service/`：产品服务模块，承载可复用业务能力。
- `command/`：产品命令常量与命令处理入口。

产品 backend 由 `../product.manifest.json` 声明，ApplicationHost 启动产品时按 manifest 加载模块并回放注册表。
