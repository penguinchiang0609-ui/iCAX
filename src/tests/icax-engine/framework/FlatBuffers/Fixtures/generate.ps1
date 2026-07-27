[CmdletBinding()]
param(
    [string]$FlatcPath
)

$ErrorActionPreference = 'Stop'

$thirdPartyRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..\..\..\third_party\flatbuffers'))

if ([string]::IsNullOrWhiteSpace($FlatcPath)) {
    & (Join-Path $thirdPartyRoot 'build-flatc.ps1')
    $workspaceRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $thirdPartyRoot '..\..\..'))
    $FlatcPath = Join-Path $workspaceRoot (
        '.codex_tmp\flatbuffers-v25.12.19\build\Release\flatc.exe')
}

if (-not (Test-Path -LiteralPath $FlatcPath)) {
    throw "flatc does not exist: $FlatcPath"
}

$schemaPath = Join-Path $PSScriptRoot 'TransportPayload.fbs'
$outputPath = Join-Path $PSScriptRoot 'Generated'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

& $FlatcPath --cpp --scoped-enums -o $outputPath $schemaPath
if ($LASTEXITCODE -ne 0) {
    throw 'Transport fixture C++ generation failed.'
}

Write-Output 'Transport fixture generated code succeeded.'
