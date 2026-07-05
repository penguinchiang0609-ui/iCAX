# OpenCascadeResourceImport

本目录提供基于 Open CASCADE Technology 的通用资源导入插件。

## 职责

- 注册 `IResourceImporter`，为当前产品的 `ResourceLibrary` 增加 STEP/STP、IGS/IGES 导入能力。
- 将外部 CAD 文件导入为通用资源：
  - `source`：`iCAX::Resource::CBinaryResource`，保存原始文件内容。
  - `geometry.brep`：`iCAX::GeometryData::BRepModel`，保存中立 BRep 数据。
- 不创建产品 Entity，不写 CAM 刀路或拓扑索引，不暴露 OCC 类型。

## 扩展方式

新增资源格式时，新建插件并实现 `IResourceImporter` 或 `IResourceExporter`，再通过 `ICAX_REGISTER_RESOURCE_IMPORTER` / `ICAX_REGISTER_RESOURCE_EXPORTER` 注册。产品 manifest 只负责加载插件 DLL，framework 会按模块路径把注册回放到当前产品的资源注册表。

## 目录结构

- `OpenCascadeResourceImporter.cpp`：OCCT 资源导入器实现。
- `OpenCascadeResourceImport.vcxproj`：插件 DLL 工程。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Windows DLL 工程基础文件。

