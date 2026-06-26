# iCAX-Application

`iCAX-Application` 是进程级应用层，负责把 Engine 和 UI 组合成一个完整桌面应用。

它不属于 `iCAX-Engine`，也不属于 `iCAX-UI`：

- `iCAX-Engine` 负责后台框架、产品运行时、项目、数据和资源。
- `iCAX-UI` 负责具体 UI 技术适配与前端 SDK。
- `iCAX-Application` 负责拥有两者之间的应用级生命周期和通用桥接。

## 目录结构

```text
iCAX-Application/
  Application/
```
