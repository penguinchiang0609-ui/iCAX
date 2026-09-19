param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$SourceDirectory = Join-Path $RepoRoot "src\apps\tube-designer\templates"
$TargetDirectory = Join-Path (Resolve-Path $OutputDirectory).Path "apps\tube-designer\templates"

if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
    throw "TubeDesigner template source directory was not found: $SourceDirectory"
}

New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File | ForEach-Object {
    $RelativePath = $_.FullName.Substring($SourceDirectory.Length).TrimStart('\')
    $Destination = Join-Path $TargetDirectory $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
}

Write-Output "SYNC TubeDesigner templates: $SourceDirectory -> $TargetDirectory"
