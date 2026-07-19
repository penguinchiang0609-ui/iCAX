# InputPDO

`InputPDO` 定义前端输入状态快照的 PDO 二进制契约。

它只表达当前键盘、鼠标和 viewport 输入状态，不承担可靠事件队列职责。点击、文本输入、快捷命令等不可丢事件仍应通过 Facade 发送。

目录结构：

- `InputPDOTypes.h`：输入 PDO 基础类型、常量和标志位。
- `InputPDOLayouts.h`：`input.state` 的固定二进制 layout。
- `InputPDODecl.*`：输入 PDO 槽声明辅助。
- `InputPDOValidation.*`：payload 校验。
- `InputPDO.h`：统一入口头。
