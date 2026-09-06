# ProductContext

`ProductContext` 是 framework 层的产品公共契约项目。

它只表达产品定义、产品用户数据和产品运行上下文接口，不负责产品模块加载、邮箱处理、项目打开等运行时行为。`ProductRuntime` 在 `Product` 项目中实现 `IProductContext`。

产品身份只有一个来源：产品清单解析出的 `CProductDefinition::ProductID`。产品清单的 `userData.features` 同时声明 FeatureID、RecordType、允许的 SubjectType、Payload schema version、基数和关系类型。ApplicationRuntime 将这些声明转换为产品作用域的 `IProductUserDataStore`，ProductRuntime 会校验作用域与自身身份一致后才接受。产品模块通过 `IProductContext::GetUserDataStore()` 访问用户记录，不能硬编码产品 ID、直接操作表，或者写入未声明的数据类型。

一条用户记录由 `ProductID / FeatureID / RecordType / RecordID` 唯一标识；`SubjectType / SubjectID` 表示它主要作用于谁，附加的客户、材料、机器等关联进入显式 Links。`RecordType` 为 single 时，RecordID 必须等于 SubjectID，从结构上保证每个作用对象只有一份设置；multiple 类型通常使用 UUID。

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

- `ProductDefinition.h`：产品静态定义、项目文件 magic、前端入口、模块配置和用户数据描述。
- `ProductData.h` / `ProductData.cpp`：产品级用户数据、最近项目和默认文件存储。
- `IProductContext.h` / `IProductContext.cpp`：产品运行上下文接口，包含产品定义、产品数据、产品级设置和已绑定产品身份的用户数据入口。
- `ProductContextExport.h`：DLL 导出宏。
