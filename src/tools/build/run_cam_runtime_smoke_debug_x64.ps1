param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")

$MSBuildCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
)

$MSBuild = $MSBuildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $MSBuild) {
    throw "MSBuild.exe was not found in known Visual Studio locations."
}

function Invoke-MSBuildProject {
    param([string]$Project)

    $ProjectPath = Join-Path $RepoRoot $Project
    Write-Output "BUILD $Project"
    & $MSBuild $ProjectPath /p:Configuration=$Configuration /p:Platform=$Platform /m:1 /nr:false /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $Project"
    }
}

Invoke-MSBuildProject "src\tests\icax-engine\framework\PDO\PDOTest\PDOTest.vcxproj"
Invoke-MSBuildProject "src\tests\icax-plugins\cam\CamRuntime\CamRuntimeTest\CamRuntimeTest.vcxproj"

$RuntimeDirectory = Join-Path $RepoRoot "src\$Platform\$Configuration"
$PDOTest = Join-Path $RuntimeDirectory "PDOTest.exe"
$CamRuntimeTest = Join-Path $RuntimeDirectory "CamRuntimeTest.exe"

Write-Output "TEST PDOTest"
& $PDOTest
if ($LASTEXITCODE -ne 0) {
    throw "PDOTest failed."
}

Write-Output "TEST CamRuntimeTest"
& $CamRuntimeTest
if ($LASTEXITCODE -ne 0) {
    throw "CamRuntimeTest failed."
}

Write-Output "OK CamRuntime smoke tests"
