# Google OR-Tools

The tube-nesting exact and large-neighborhood constraint backend uses the
official OR-Tools C++ binary distribution for Visual Studio 2022 x64.

- Pinned version: `9.12.4544`
- License: Apache-2.0 (the upstream package contains `LICENSE`)
- Expected archive SHA-256:
  `67CCE7973C26FD3E72053C616FA4372B2E04F0B56CCB582DEF1B59C1D43893AF`

Prepare the local dependency from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File src\third_party\ortools\prepare-ortools.ps1
```

The verified package is installed under `install/vs2022-x64`; generated
downloads and install products are ignored. `OrTools.props` compiles the CP-SAT
backend for non-Debug x64 builds when that install is present. The official
archive uses the release CRT, so Debug builds deliberately use the independent
exact-enumeration fallback instead of mixing incompatible STL/protobuf ABIs.
Final DLL/EXE projects import
`OrTools.Runtime.targets` to copy the required runtime DLLs.
