# iCAX Engine

本目录记录 iCAX Engine 的规格、方案和概要文档，只覆盖 C++ backend 与 engine framework。

## 目录结构

- `整体架构与产品扩展指南.md`：backend、UI、产品扩展之间的整体关系、运行流程和通信模型。
- `Backend概要文档.md`：当前 C++ backend 的总体结构、启动流程、通信模型、数据模型、资源模型和线程模型。
- `Products/`：基于 iCAX Engine 的具体产品设计文档，例如平面激光 CAM。
- `EC介绍.md`：Entity-Component 数据表达的基础说明。
- `Foundation/`：不依赖 framework 的基础能力文档。
- `Framework/`：ApplicationRuntime、Product、Project、Database、Resources、Facades、PDO、Facades、Behaviour、Services 等框架能力文档。

H5 前端公共框架文档位于同级 `doc/iCAX-UI/`。
