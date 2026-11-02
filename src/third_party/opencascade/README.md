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
- `geometry/OpenCascadeGeometryServices` supplies reusable, stateless `IService` capabilities through neutral `framework/GeometryServices` contracts; its first capability evaluates directional offsets of 3D curves.
- Product code should depend on `Scene.Resources().Import<T>()` and neutral resources, not OCCT classes.

## Build

Run from the repository root:

```powershell
pwsh -NoProfile -File src\tools\build\build-opencascade.ps1 -Configuration Debug
pwsh -NoProfile -File src\tools\build\build-opencascade.ps1 -Configuration Release
```

The script requires PowerShell 7 and CMake. It generates Visual Studio 2022 x64 build files and installs OCCT into:

```text
src/third_party/opencascade/install/vs2022-x64
```

Generated `build/` and `install/` directories are ignored because they are local build products.

The build selects OCCT's `FLEXIBLE` memory manager, with its built-in recyclable
allocator as the default (`MMGT_OPT=1`). Small geometry allocations reuse pools
instead of contending on the Debug CRT heap. Allocation and release remain
inside `TKernel`; the Debug CRT and C++ ABI are unchanged. Aligned allocations
retain their original matching allocate/free functions.

`BRepMesh_Delaun::isBoundToFrontier` uses a small local link stack, growing when
needed, to avoid allocating a deque for every vertex fan. Its LIFO order and
meshing tolerances are unchanged.

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
