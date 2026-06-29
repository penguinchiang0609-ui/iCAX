# iCAX-Plugins

`iCAX-Plugins` 存放可选扩展能力。

插件可以依赖 `iCAX-Engine` 的 foundation/framework 能力，但 `iCAX-Engine` 不反向依赖插件。这里适合放渲染、几何内核适配、导入导出、行业功能等可替换能力。

## 目录结构

- `physics/`：后端物理、碰撞和拾取相关插件。
- `render/`：渲染相关插件。
- `input/`：前端输入状态与输入协议相关插件。
