# Mailbox Protocol

本目录描述平面激光 CAM 通过 mailbox 发送的产品命令。

## 初始命令

| 命令 | TypeCode | 说明 |
| --- | --- | --- |
| `flatLaser.importDrawing` | `0x464C43414D000001` | 导入 DXF、DWG、SVG 等二维图形。 |
| `flatLaser.createSheet` | `0x464C43414D000002` | 创建板材。 |
| `flatLaser.generateNesting` | `0x464C43414D000003` | 生成排样结果。 |
| `flatLaser.generateToolpath` | `0x464C43414D000004` | 生成刀路。 |
| `flatLaser.runSimulation` | `0x464C43414D000005` | 运行加工仿真。 |
| `flatLaser.generateNc` | `0x464C43414D000006` | 生成 NC 文件。 |

所有命令都通过产品所属的 mail channel 发送，不直接进入 ApplicationHost 的系统通道。

当前实现为产品骨架 handler：命令可注册、可被调度，并返回 accepted 响应。真实业务结果后续由产品 Service 写入 Repository、ResourceLibrary 和 PDO。
