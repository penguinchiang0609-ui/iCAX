# Flat Laser CAM

本目录承载平面激光 CAM 产品的全部产品侧代码与配置。

产品信息不得写入 `src/icax/framework`。Framework 只提供通用的 Application、Product、Project、Database、Resource、Channel、Command 等基础机制；本目录负责声明本产品的组件、行为、服务、命令、前端入口和文件识别信息。

## 目录结构

- `backend/`：C++ 产品后端模块。
- `frontend/`：H5 产品前端入口与页面模块。
- `protocol/`：前后端通信协议、PDO 数据约定和命令常量说明。

