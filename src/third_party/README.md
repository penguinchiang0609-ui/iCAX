# third_party

`third_party` stores third-party source trees that are intentionally vendored into this repository.

This directory is different from `iCAX-Engine/packages`: `packages` is the Visual Studio/NuGet package cache used by existing projects, while `third_party` is for source dependencies that need local configuration or source-level inspection.

## Directory Structure

- `opencascade/`: Open CASCADE Technology source, build script, and MSBuild integration files.
