# Mailbox

Mailbox 是 iCAX Engine framework 的低频消息通信模块。

它提供三层能力：

- `CMailQueue`：单向预分配邮件队列。
- `CMailChannel`：双向通道，拥有两个方向的队列。
- `CMailPostOffice`：某一端的轻量收发视图。

Mailbox 不绑定前端/后端，不解释业务命令，也不处理高频大数据。普通命令和事件可以使用 UTF-8 文本 payload；高频数据走 PDO。

## 文档

- `Mailbox规格文档.md`：对外使用契约、数据结构和生命周期要求。
- `Mailbox方案文档.md`：内部设计、池化队列、线程模型和测试方案。
