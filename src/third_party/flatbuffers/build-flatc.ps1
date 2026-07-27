[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$version = '25.12.19'
$sourceRoot = Join-Path $PSScriptRoot "flatbuffers-$version"
$workspaceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..'))
$buildDirectory = if ($Platform -eq 'x64') {
    'build'
}
else {
    'build-Win32'
}
$buildRoot = Join-Path $workspaceRoot (
    ".codex_tmp\flatbuffers-v$version\$buildDirectory")
$cmakePlatform = if ($Platform -eq 'Win32') { 'Win32' } else { 'x64' }
$flatcPath = Join-Path $buildRoot "$Configuration\flatc.exe"

if (-not (Test-Path -LiteralPath (
    Join-Path $sourceRoot 'include\flatbuffers\flatbuffers.h'))) {
    throw "FlatBuffers source is incomplete: $sourceRoot"
}

cmake `
    -S $sourceRoot `
    -B $buildRoot `
    -G 'Visual Studio 17 2022' `
    -A $cmakePlatform `
    -DFLATBUFFERS_BUILD_TESTS=OFF `
    -DFLATBUFFERS_BUILD_FLATLIB=OFF `
    -DFLATBUFFERS_BUILD_SHAREDLIB=OFF `
    -DFLATBUFFERS_BUILD_GRPCTEST=OFF `
    -DFLATBUFFERS_BUILD_FLATC=ON
if ($LASTEXITCODE -ne 0) {
    throw 'FlatBuffers CMake configuration failed.'
}

cmake `
    --build $buildRoot `
    --config $Configuration `
    --target flatc `
    --parallel
if ($LASTEXITCODE -ne 0) {
    throw 'flatc build failed.'
}

if (-not (Test-Path -LiteralPath $flatcPath)) {
    throw "flatc was not produced: $flatcPath"
}

Write-Output $flatcPath
