# ColliderData

`ColliderData` 是物理/碰撞插件族的中立数据契约项目。

它表达 collider、transform、raycast、layer/mask 和 owner object 映射，不依赖 Jolt、Bullet、Three.js 或具体产品。

目录结构：

- `ColliderDataTypes.h`：碰撞数据结构和基础 ID/版本类型。
- `ColliderData.h`：项目统一入口头。
- `ColliderDataExport.h`：DLL 导出宏。
