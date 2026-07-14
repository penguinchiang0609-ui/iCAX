# DAEResourceImport

## 目录意义

本目录实现 COLLADA `.dae` 几何资源导入器。它属于资源系统扩展插件，负责把外部 DAE 文件导入为中立的 `GeometryData::CTriangleMeshResource`，供渲染、碰撞或其他产品逻辑继续消费。

DAE 只是输入格式，不在机床、工件或刀路代码中表达业务含义。上层通过资源系统按 `geometry.dae` 格式 ID 选择本插件。

## 当前结构

- `DAEResourceImporter.cpp`：DAE 解析与资源导入器注册。
- `framework.h`、`pch.h`、`pch.cpp`：Windows DLL 与预编译头支持。
- `dllmain.cpp`：插件 DLL 入口。
- `DAEResourceImport.vcxproj`、`DAEResourceImport.vcxproj.filters`：Visual Studio 工程文件。

## 支持范围

- 支持 `library_geometries/geometry/mesh`。
- 支持 `triangles` 与 `polylist`，`polylist` 使用扇形三角化。
- 支持 `POSITION`/`VERTEX`、`NORMAL`、`TEXCOORD` 输入。
- 支持 `<asset><unit meter="..."/>` 单位换算。

## 不承载的职责

- 不解析动画、骨骼、控制器、复杂材质和贴图绑定。
- 不表达机床语义，SDF/URDF 等机床定义应在对应 loader 中解析。
- 不负责渲染 PDO 分配，渲染行为和渲染服务负责把资源实例同步到前端。
