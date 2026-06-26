# ProductContext

`ProductContext` 是 framework 层的产品公共契约项目。

它只表达产品定义、产品用户数据和产品运行上下文接口，不负责产品模块加载、邮箱处理、项目打开等运行时行为。`ProductRuntime` 在 `Product` 项目中实现 `IProductContext`。

## 目录结构

- `ProductDefinition.h`：产品静态定义、项目文件 magic、前端入口和模块配置。
- `ProductData.h` / `ProductData.cpp`：产品级用户数据、最近项目和默认文件存储。
- `IProductContext.h` / `IProductContext.cpp`：产品运行上下文接口。
- `ProductContextExport.h`：DLL 导出宏。
