param(
    [string]$Python = 'python',
    [string]$PackagerDependencies = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'Temp\licensing-delivery' }
$buildDirectory = Join-Path $repository 'Temp\licensing-build'
& cmake -S $PSScriptRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native configuration failed' }
& cmake --build $buildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
$oldPythonPath = $env:PYTHONPATH
try {
    if ($PackagerDependencies) { $env:PYTHONPATH = (Resolve-Path $PackagerDependencies).Path }
    $verifier = Join-Path $buildDirectory 'Release\td-license-verify-ek.exe'
    & $Python -m PyInstaller --noconfirm --onedir --windowed --name 'TubeDesigner-Issuer' `
        --distpath $OutputDirectory --workpath (Join-Path $buildDirectory 'pyinstaller-work') `
        --specpath $buildDirectory --add-binary "$verifier;." (Join-Path $PSScriptRoot 'issuer\issuer_gui.py')
    if ($LASTEXITCODE -ne 0) { throw 'Issuer packaging failed' }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'issuer\README.md') -Destination (Join-Path $OutputDirectory 'TubeDesigner-Issuer\使用说明.md')
} finally { $env:PYTHONPATH = $oldPythonPath }
Write-Output (Join-Path $OutputDirectory 'TubeDesigner-Issuer\TubeDesigner-Issuer.exe')
