# Services

本目录放置平面激光 CAM 的产品服务模块。

Service 承载可复用的产品业务能力，例如图形导入、板材校验、工艺计算、排样、刀路、后处理等。CommandHandler 可以调用 Service，但 Service 不直接依赖 mailbox。

## 目录结构

- `FlatLaserCamService/`：平面激光 CAM 服务注册模块。

