# icax-protocol

`icax-protocol` 放置 C++ backend 与 frontend bridge 共同遵守的协议约定。这里不放复杂运行逻辑，只放双方都需要稳定引用的消息名、字段名、数据形状和常量。

## 目录结构

- `mailbox/`：普通交互协议。
- `pdo/`：高频数据协议。
- `common/`：通用常量、错误码、命名规则和版本信息。
