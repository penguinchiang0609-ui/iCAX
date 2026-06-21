# Behaviours

本目录放置平面激光 CAM 的产品行为模块。

Behaviour 只响应组件生命周期与组件变化，不直接承担命令分发。命令入口放在 `backend/command`，可复用业务能力放在 `backend/service`。

## 目录结构

- `FlatLaserCamBehaviour/`：平面激光 CAM 行为注册模块。

