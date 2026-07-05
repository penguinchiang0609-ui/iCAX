# Open CASCADE Technology

This directory integrates Open CASCADE Technology (OCCT) as the CAD geometry kernel source dependency.

## Version

- Upstream: Open CASCADE Technology
- Version: `8.0.0-p1`
- Upstream tag: `V8_0_0_p1`
- Source directory: `occt-8.0.0-p1/`

`V8_0_0_p1` was selected from the upstream latest GitHub release on 2026-07-05.

## Role In iCAX

OCCT is an implementation dependency of geometry adapter and CAD import plugins. It must not leak into framework data contracts:

- `GeometryData` remains the neutral iCAX geometry data model.
- `GeometryAlgo` owns generic geometry operations.
- `GeometryAdapter` or product CAD import plugins may call OCCT and convert results into `GeometryData`.
- Product code should depend on `ICadImportService` or neutral resources, not OCCT classes.

## Build

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File src\tools\build\build-opencascade.ps1 -Configuration Debug
```

The script generates Visual Studio 2022 x64 build files and installs OCCT into:

```text
src/third_party/opencascade/install/vs2022-x64
```

Generated `build/` and `install/` directories are ignored because they are local build products.

## MSBuild Integration

Projects that need only include/lib/bin paths can import:

```xml
<Import Project="..\..\..\third_party\opencascade\OpenCascade.props" />
```

Projects that need STEP/IGES/DataExchange link libraries can import:

```xml
<Import Project="..\..\..\third_party\opencascade\OpenCascade.DataExchange.props" />
<Import Project="..\..\..\third_party\opencascade\OpenCascade.Runtime.targets" />
```

The runtime target copies OCCT DLLs from the local install directory to the project output directory.
