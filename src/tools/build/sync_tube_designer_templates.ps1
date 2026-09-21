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
$sourceFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File | ForEach-Object {
    $RelativePath = $_.FullName.Substring($SourceDirectory.Length).TrimStart('\')
    [void]$sourceFiles.Add($RelativePath)
    $Destination = Join-Path $TargetDirectory $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
}

# The runtime template tree is a deployed mirror, not an overlay.  Remove files
# whose source template was deleted so retired resource cards cannot survive a
# rebuild in the binary directory.
Get-ChildItem -LiteralPath $TargetDirectory -Recurse -File | ForEach-Object {
    $RelativePath = $_.FullName.Substring($TargetDirectory.Length).TrimStart('\')
    if (-not $sourceFiles.Contains($RelativePath)) {
        Remove-Item -LiteralPath $_.FullName -Force
    }
}

# Clean empty directories left by removed template packages, deepest first.
Get-ChildItem -LiteralPath $TargetDirectory -Recurse -Directory |
    Sort-Object { $_.FullName.Length } -Descending |
    ForEach-Object {
        if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) {
            Remove-Item -LiteralPath $_.FullName -Force
        }
    }

Write-Output "SYNC TubeDesigner templates: $SourceDirectory -> $TargetDirectory"
