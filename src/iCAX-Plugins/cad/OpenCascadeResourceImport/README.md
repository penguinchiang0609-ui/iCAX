# OpenCascadeResourceImport

本目录提供基于 Open CASCADE Technology 的通用资源导入插件。

## 职责

- 注册 `IResourceImporter`，为当前产品的 `ResourceLibrary` 增加 STEP/STP、IGS/IGES 导入能力。
- 将外部 CAD 文件导入为通用资源：
  - `source`：`iCAX::Resource::CBinaryResource`，保存原始文件内容。
  - `geometry.brep`：`iCAX::GeometryData::BRepModel`，保存中立 BRep 数据。
- 不创建产品 Entity，不写 CAM 刀路或拓扑索引，不暴露 OCC 类型。

## 扩展方式

新增资源格式时，新建插件并实现 `IResourceImporter` 或 `IResourceExporter`，再通过 `ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER` / `ICAX_REGISTER_RESOURCE_EXPORTER_PROVIDER` 注册稳定 provider ID。本插件 provider ID 为 `occ.opencascade`。

产品 manifest 通过 `backend.resources.handlers` 选择资源类型、格式、扩展名和 provider。例如 STEP 导入到中立 BRep：

```json
{
  "kind": "importer",
  "resourceType": "geometry.brep",
  "formatId": "cad.step",
  "extensions": [".step", ".stp"],
  "module": "../../iCAX-Plugins/cad/OpenCascadeResourceImport/${Platform}/${Configuration}/OpenCascadeResourceImport.dll",
  "provider": "occ.opencascade",
  "priority": 100
}
```

运行期由 `ProductRuntime` 加载 DLL，再按模块路径把注册回放到当前产品和 Scene 的资源注册表。产品代码只需要调用 `scene.Resources().Import<iCAX::GeometryData::BRepModel>(path)` 或传入 `CResourceImportRequest`。

## 目录结构

- `OpenCascadeResourceImporter.cpp`：OCCT 资源导入器实现。
- `OpenCascadeResourceImport.vcxproj`：插件 DLL 工程。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Windows DLL 工程基础文件。
