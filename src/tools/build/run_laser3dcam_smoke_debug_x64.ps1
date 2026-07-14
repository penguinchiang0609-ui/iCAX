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

function Add-PathIfExists {
    param([string]$RelativePath)

    $Path = Join-Path $RepoRoot $RelativePath
    if (Test-Path -LiteralPath $Path) {
        $script:RuntimePaths += (Resolve-Path $Path).Path
    }
}

Invoke-MSBuildProject "src\tests\icax-engine\framework\PDO\PDOTest\PDOTest.vcxproj"
Invoke-MSBuildProject "src\tests\icax-plugins\cam\Laser3DCAM\Laser3DCAMTest\Laser3DCAMTest.vcxproj"

$script:RuntimePaths = @()
@(
    "src\iCAX-Engine\foundation\Data\$Platform\$Configuration",
    "src\iCAX-Engine\foundation\GeometryData\$Platform\$Configuration",
    "src\iCAX-Engine\framework\ApplicationContext\$Platform\$Configuration",
    "src\iCAX-Engine\framework\ProductContext\$Platform\$Configuration",
    "src\iCAX-Engine\framework\ProjectContext\$Platform\$Configuration",
    "src\iCAX-Engine\framework\CommandTargets\$Platform\$Configuration",
    "src\iCAX-Engine\framework\Database\$Platform\$Configuration",
    "src\iCAX-Engine\framework\Resources\$Platform\$Configuration",
    "src\iCAX-Engine\framework\Services\$Platform\$Configuration",
    "src\iCAX-Engine\framework\Mailbox\$Platform\$Configuration",
    "src\iCAX-Engine\framework\PDO\$Platform\$Configuration",
    "src\iCAX-Engine\framework\Behaviour\$Platform\$Configuration",
    "src\iCAX-Plugins\common\Transform\$Platform\$Configuration",
    "src\iCAX-Plugins\render\RenderData\$Platform\$Configuration",
    "src\iCAX-Plugins\render\RenderInteraction\$Platform\$Configuration",
    "src\iCAX-Plugins\render\RenderService\$Platform\$Configuration",
    "src\iCAX-Plugins\render\CameraNavigation\$Platform\$Configuration",
    "src\iCAX-Plugins\input\InputPDO\$Platform\$Configuration",
    "src\iCAX-Plugins\input\InputService\$Platform\$Configuration",
    "src\iCAX-Plugins\cam\Laser3DCAM\$Platform\$Configuration"
) | ForEach-Object { Add-PathIfExists $_ }

$env:PATH = ($RuntimePaths -join ";") + ";" + $env:PATH

$PDOTest = Join-Path $RepoRoot "src\tests\icax-engine\framework\PDO\PDOTest\$Platform\$Configuration\PDOTest.exe"
$Laser3DCAMTest = Join-Path $RepoRoot "src\tests\icax-plugins\cam\Laser3DCAM\Laser3DCAMTest\$Platform\$Configuration\Laser3DCAMTest.exe"

Write-Output "TEST PDOTest"
& $PDOTest
if ($LASTEXITCODE -ne 0) {
    throw "PDOTest failed."
}

Write-Output "TEST Laser3DCAMTest"
& $Laser3DCAMTest
if ($LASTEXITCODE -ne 0) {
    throw "Laser3DCAMTest failed."
}

Write-Output "OK Laser3DCAM smoke tests"
