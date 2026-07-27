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

$schemaRoot = Join-Path $PSScriptRoot 'Schema'
$v1Schema = Join-Path $schemaRoot 'TestEnvelopeV1.fbs'
$v2Schema = Join-Path $schemaRoot 'TestEnvelopeV2.fbs'
$v1Output = Join-Path $PSScriptRoot 'Generated\V1'
$v2Output = Join-Path $PSScriptRoot 'Generated\V2'

New-Item -ItemType Directory -Force -Path $v1Output | Out-Null
New-Item -ItemType Directory -Force -Path $v2Output | Out-Null

& $FlatcPath --cpp --scoped-enums -o $v1Output $v1Schema
if ($LASTEXITCODE -ne 0) {
    throw 'V1 C++ generation failed.'
}

& $FlatcPath --cpp --scoped-enums -o $v2Output $v2Schema
if ($LASTEXITCODE -ne 0) {
    throw 'V2 C++ generation failed.'
}

& $FlatcPath --conform $v1Schema $v2Schema
if ($LASTEXITCODE -ne 0) {
    throw 'V2 schema is not compatible with V1.'
}

Write-Output 'FlatBuffers generated code and compatibility check succeeded.'
