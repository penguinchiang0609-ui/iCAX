# input plugins

本目录放置输入相关的插件契约与实现。

目录结构：

- `InputPDO/`：前端写入、后端读取的输入状态 PDO 契约。
- `InputService/`：后端输入状态服务，从 Scene 级 InputPDO 读取键鼠状态，并提供 `GetKey`、`GetKeyDown`、`GetKeyUp`。
