# ProductContext

`ProductContext` 是 framework 层的产品公共契约项目。

它只表达产品定义、产品用户数据和产品运行上下文接口，不负责产品模块加载、邮箱处理、项目打开等运行时行为。`ProductRuntime` 在 `Product` 项目中实现 `IProductContext`。

产品级设置是与某个产品业务相关、但不跟随某一个项目文件保存的数据。它跟随产品和用户环境走。不同产品可以写入不同字段，统一通过 `CProductData::Settings` 承载，并通过 `IProductContext::GetSettings/ReplaceSettings` 访问。

典型产品级应用参数：

- 最近打开项目列表。
- 产品 UI 偏好。
- 默认导入目录。
- 默认刀具库路径。
- 产品级默认参数模板。

不应放入 ProductContext 的数据：

- 跟项目文件一起保存和打开的项目参数。
- 工件、切割头、刀路、图层、运动规划等项目业务对象。
- 会参与项目撤销还原的业务数据。

基本分界线：

```text
ApplicationContext / ApplicationSettings -> iCAX 应用程序参数，和任何产品无关
ProductContext / ProductData             -> 产品级应用参数，跟产品和用户环境走
ProjectContext / Settings                -> 项目级参数，跟项目文件走
Repository / Entity                      -> 项目业务对象
Resources                                -> 项目内嵌资源
```

## 目录结构

- `ProductDefinition.h`：产品静态定义、项目文件 magic、前端入口和模块配置。
- `ProductData.h` / `ProductData.cpp`：产品级用户数据、最近项目和默认文件存储。
- `IProductContext.h` / `IProductContext.cpp`：产品运行上下文接口，包含产品定义、产品数据和产品级设置访问入口。
- `ProductContextExport.h`：DLL 导出宏。
