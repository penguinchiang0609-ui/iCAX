# Transform

`Transform` 提供 Entity 的通用空间切面。它只表达位置、姿态和缩放，不属于渲染，也不属于物理。

- `CTransformComponent`：通用 Transform 组件，供 Render、Collider、Camera、MachineLink/Joint/Visual、Workpiece 等切面共同消费。
- `Transform.h`：统一入口头。
- `TransformExport.h`：DLL 导出宏。

当前字段为非持久化可观察字段，会触发事件和版本，适合 PDO、渲染和物理等运行时同步。需要跟随项目文件保存的工件摆放策略，后续应在产品层明确决定是否引入持久化姿态数据或调整字段策略。
