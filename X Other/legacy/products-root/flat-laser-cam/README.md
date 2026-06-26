# Flat Laser CAM

`flat-laser-cam` 是平面激光 CAM 产品目录示例。它展示具体产品如何同时扩展 backend 和 webpage。

## 目录结构

- `product.manifest.json`：产品定义。
- `backend/`：产品 backend 扩展模块。
- `webpage/`：产品 H5 模块入口和产品界面贡献。
- `protocol/`：产品命令、PDO 和文件格式约定。

产品目录不属于 `src/icax-engine/framework`，也不属于 `src/iCAX-UI`。底层框架只通过 manifest 装载产品。
