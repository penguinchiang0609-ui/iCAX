# ColliderService

`ColliderService` 是物理/碰撞插件族的服务接口项目。

它定义 project/scene 级 collider 生命周期、raycast 和 step 接口。接口不依赖具体物理引擎，底层可以用 Jolt、Bullet、Embree 或自研 BVH。

目录结构：

- `IColliderService.h`：碰撞服务接口。
- `ColliderService.h`：项目统一入口头。
- `ColliderServiceExport.h`：DLL 导出宏。
