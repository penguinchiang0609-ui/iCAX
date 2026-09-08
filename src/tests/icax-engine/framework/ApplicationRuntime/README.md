# ApplicationRuntime Tests

本目录存放 `framework/ApplicationRuntime` 项目的单元测试。

## 目录结构

- `ApplicationRuntimeTest/`: 使用 gtest 验证应用运行时生命周期、应用级邮箱入口和内置应用命令。

`ApplicationRuntimeSDOTest.Unicode*` 覆盖 UTF-8 中文／特殊字符目录及项目名：
项目签名识别、缺失文件和目录拒绝、保存后直接打开及工作台 SDO 打开、默认名称保真、快速保存日志恢复。
