# SDO（Service Data Object）

对应源码目录 `src/iCAX-Engine/framework/SDO/`。SDO 定义产品和宿主对外提供的 `SDOName.MethodName` 非周期服务协议，与 PDO 的高频数据通道形成清晰分工。

- `SDO规格文档.md`：对外协议的规范性文档，定义边界、名称、调用、payload、状态、并发和版本演进。
- `SDO方案文档.md`：定位、调用模型、上下文和产品所有权。

SDO 只借用 EtherCAT 的 SDO/PDO 分层思路，不是 EtherCAT 或 CoE 的线协议实现。
