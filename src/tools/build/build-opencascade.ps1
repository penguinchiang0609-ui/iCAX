#requires -Version 7.0
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [switch]$Clean,

    [ValidateRange(1, 64)]
    [int]$Parallel = 4
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
    # The desktop uses the Debug kernel for interactive work. Keep its CRT,
    # assertions and symbols, but optimize geometric algorithms instead of
    # spending tens of seconds in unoptimized boolean and surface routines.
    "-DCMAKE_CXX_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2",
    "-DCMAKE_C_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2",
    "-DINSTALL_DIR=$installDir",
    "-DINSTALL_DIR_LAYOUT=Windows"
)

function Invoke-OCCTCMake([string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command cmake -CommandType Application).Source)
    $start.UseShellExecute = $false
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ($entry.Key -ine 'Path' -and $seen.Add([string]$entry.Key)) {
            $start.Environment[[string]$entry.Key] = [string]$entry.Value
        }
    }
    $start.Environment['Path'] = [Environment]::GetEnvironmentVariable('Path')
    # Do not reuse MSBuild workers started by another host with a bad environment.
    $start.Environment['MSBUILDDISABLENODEREUSE'] = '1'
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "OCCT CMake failed with exit code $($process.ExitCode)" }
    } finally { $process.Dispose() }
}

Invoke-OCCTCMake $configureArgs
Invoke-OCCTCMake @('--build', $buildDir, '--config', $Configuration, '--target', 'INSTALL',
    '--parallel', [string]$Parallel, '--', '/nr:false')

Write-Host "OCCT $Configuration installed to $installDir"
