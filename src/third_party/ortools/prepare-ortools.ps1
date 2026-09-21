param(
    [string]$ArchivePath = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$version = "9.12.4544"
$archiveName = "or-tools_x64_VisualStudio2022_cpp_v$version.zip"
$downloadUrl = "https://github.com/google/or-tools/releases/download/v9.12/$archiveName"
$expectedSha256 = "67CCE7973C26FD3E72053C616FA4372B2E04F0B56CCB582DEF1B59C1D43893AF"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$downloadDirectory = Join-Path $scriptRoot "download"
$installDirectory = Join-Path $scriptRoot "install\vs2022-x64"
$extractDirectory = Join-Path $scriptRoot "_extract_$version"

$scriptRootFull = [System.IO.Path]::GetFullPath($scriptRoot).TrimEnd('\') + '\'
foreach ($managedPath in @($downloadDirectory, $installDirectory, $extractDirectory)) {
    $managedPathFull = [System.IO.Path]::GetFullPath($managedPath)
    if (-not $managedPathFull.StartsWith($scriptRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to manage a path outside the OR-Tools dependency directory: $managedPathFull"
    }
}

if (-not $ArchivePath) {
    New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null
    $ArchivePath = Join-Path $downloadDirectory $archiveName
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        Invoke-WebRequest -Uri $downloadUrl -OutFile $ArchivePath
    }
}

$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$actualSha256 = (Get-FileHash -LiteralPath $resolvedArchive -Algorithm SHA256).Hash.ToUpperInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "OR-Tools archive hash mismatch. Expected $expectedSha256, got $actualSha256."
}

if ((Test-Path -LiteralPath $installDirectory) -and -not $Force) {
    $header = Join-Path $installDirectory "include\ortools\sat\cp_model.h"
    $library = Join-Path $installDirectory "lib\ortools.lib"
    if ((Test-Path -LiteralPath $header) -and (Test-Path -LiteralPath $library)) {
        Write-Output "OR-Tools $version is already prepared at $installDirectory"
        exit 0
    }
    throw "Incomplete OR-Tools install exists at $installDirectory. Re-run with -Force."
}

if (Test-Path -LiteralPath $extractDirectory) {
    Remove-Item -LiteralPath $extractDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $extractDirectory | Out-Null
Expand-Archive -LiteralPath $resolvedArchive -DestinationPath $extractDirectory -Force

$packageRoot = Get-ChildItem -LiteralPath $extractDirectory -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "include\ortools\sat\cp_model.h") } |
    Select-Object -First 1
if (-not $packageRoot) {
    throw "The verified archive did not contain the expected OR-Tools C++ package."
}

if (Test-Path -LiteralPath $installDirectory) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
Copy-Item -Path (Join-Path $packageRoot.FullName "*") -Destination $installDirectory -Recurse -Force
Remove-Item -LiteralPath $extractDirectory -Recurse -Force

Write-Output "Prepared OR-Tools $version at $installDirectory"
