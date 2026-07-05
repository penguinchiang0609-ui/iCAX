param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$sourceDir = Join-Path $repoRoot "src\third_party\opencascade\occt-8.0.0-p1"
$buildDir = Join-Path $repoRoot "src\third_party\opencascade\build\vs2022-x64"
$installDir = Join-Path $repoRoot "src\third_party\opencascade\install\vs2022-x64"

if (-not (Test-Path $sourceDir)) {
    throw "OCCT source directory was not found: $sourceDir"
}

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

$configureArgs = @(
    "-S", $sourceDir,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DBUILD_LIBRARY_TYPE=Shared",
    "-DBUILD_MODULE_FoundationClasses=ON",
    "-DBUILD_MODULE_ModelingData=ON",
    "-DBUILD_MODULE_ModelingAlgorithms=ON",
    "-DBUILD_MODULE_DataExchange=ON",
    "-DBUILD_MODULE_Visualization=OFF",
    "-DBUILD_MODULE_ApplicationFramework=OFF",
    "-DBUILD_MODULE_Draw=OFF",
    "-DUSE_TCL=OFF",
    "-DUSE_FREETYPE=OFF",
    "-DUSE_FREEIMAGE=OFF",
    "-DUSE_OPENVR=OFF",
    "-DUSE_FFMPEG=OFF",
    "-DUSE_TBB=OFF",
    "-DUSE_VTK=OFF",
    "-DUSE_RAPIDJSON=OFF",
    "-DUSE_DRACO=OFF",
    "-DUSE_GLES2=OFF",
    "-DUSE_D3D=OFF",
    "-DINSTALL_DIR=$installDir",
    "-DINSTALL_DIR_LAYOUT=Windows"
)

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "OCCT CMake configure failed with exit code $LASTEXITCODE"
}

& cmake --build $buildDir --config $Configuration --target INSTALL --parallel
if ($LASTEXITCODE -ne 0) {
    throw "OCCT build failed with exit code $LASTEXITCODE"
}

Write-Host "OCCT $Configuration installed to $installDir"
