param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

function Get-TemplateFileHash([string]$Path) {
    # MSBuild launches Windows PowerShell even when the caller is PowerShell 7.
    # Use the shared .NET API rather than depending on its inherited modules.
    $Stream = [IO.File]::OpenRead($Path)
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($Hasher.ComputeHash($Stream)) }
    finally { $Stream.Dispose(); $Hasher.Dispose() }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$SourceDirectory = Join-Path $RepoRoot "src\apps\tube-designer\templates"
$ShapeCatalogue = [IO.File]::ReadAllText((Join-Path $SourceDirectory 'finished-product\shapes.json')) | ConvertFrom-Json
$BrowserCataloguePath = Join-Path $RepoRoot 'src\apps\tube-designer\webpage\finishedProductShapes.generated.mjs'
$BrowserCatalogueText = [IO.File]::ReadAllText($BrowserCataloguePath)
if ($BrowserCatalogueText -notmatch '(?s)export default\s+(\{.*\});\s*$') {
    throw 'Invalid browser finished-product catalogue. Regenerate with generate_finished_product_shapes.py.'
}
$BrowserCatalogue = $Matches[1] | ConvertFrom-Json
if (($ShapeCatalogue | ConvertTo-Json -Depth 100 -Compress) -cne
    ($BrowserCatalogue | ConvertTo-Json -Depth 100 -Compress)) {
    throw 'Browser finished-product catalogue differs from source. Run src/tools/build/generate_finished_product_shapes.py before deployment.'
}
$OutputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$TargetDirectory = [IO.Path]::GetFullPath((Join-Path $OutputRoot "apps\tube-designer\templates"))

if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
    throw "TubeDesigner template source directory was not found: $SourceDirectory"
}

# The destination is an explicitly named runtime tree. Never mirror over the
# source, or follow a directory link while copying or removing stale files.
$OutputPrefix = $OutputRoot.TrimEnd('\') + '\'
$TargetPrefix = $TargetDirectory.TrimEnd('\') + '\'
if (-not $TargetDirectory.StartsWith($OutputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $SourceDirectory.Equals($TargetDirectory, [StringComparison]::OrdinalIgnoreCase) -or
    $SourceDirectory.StartsWith($TargetPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Invalid TubeDesigner template deployment destination: $TargetDirectory"
}
$Directory = $TargetDirectory
while ($Directory -and ($Directory.Equals($OutputRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $Directory.StartsWith($OutputPrefix, [StringComparison]::OrdinalIgnoreCase))) {
    if ((Test-Path -LiteralPath $Directory) -and
        ((Get-Item -LiteralPath $Directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Template deployment must not follow directory links: $Directory"
    }
    $Directory = Split-Path -Parent $Directory
}
if (Test-Path -LiteralPath $TargetDirectory) {
    $LinkedDirectory = Get-ChildItem -LiteralPath $TargetDirectory -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
    if ($LinkedDirectory) { throw "Template deployment must not follow directory links: $($LinkedDirectory.FullName)" }
}

# Each independent-product template owns both parts of the selection contract.
# Validate the source before touching an existing runtime installation.
$AssemblyManifests = @(Get-ChildItem -LiteralPath (Join-Path $SourceDirectory 'assembly') -Recurse -Filter assembly.json -File)
$ProductTemplateCount = 0
foreach ($Manifest in $AssemblyManifests) {
    $Descriptor = [IO.File]::ReadAllText($Manifest.FullName) | ConvertFrom-Json
    if ($null -eq $Descriptor.inputContract -or $null -eq $Descriptor.exampleInput) {
        throw "Assembly template '$($Descriptor.id)' is missing its current inputContract or exampleInput."
    }
    ++$ProductTemplateCount
    foreach ($Hook in @('applicability.py', 'example.py')) {
        $HookPath = Join-Path $Manifest.DirectoryName $Hook
        if (-not (Test-Path -LiteralPath $HookPath -PathType Leaf)) {
            throw "Assembly template '$($Descriptor.id)' is missing $Hook`: $HookPath"
        }
    }
}
foreach ($Shared in @('assembly_template_runtime.py', 'finished_product_runtime.py',
    'assembly_applicability_geometry.py', 'assembly_example_product.py',
    'assembly_geometry_process_runtime.py', 'assembly_tube_machining.py',
    'assembly_surface_machining.py', 'assembly_structural_machining.py')) {
    $SharedPath = Join-Path $SourceDirectory "_shared\$Shared"
    if (-not (Test-Path -LiteralPath $SharedPath -PathType Leaf)) {
        throw "Shared assembly template runtime is missing: $SharedPath"
    }
}

$Files = @(Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') })
if (-not $ValidateOnly) {
    New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
}
$sourceFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($File in $Files) {
    $RelativePath = $File.FullName.Substring($SourceDirectory.Length).TrimStart('\')
    [void]$sourceFiles.Add($RelativePath)
    $Destination = Join-Path $TargetDirectory $RelativePath
    if (-not $ValidateOnly) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
        Copy-Item -LiteralPath $File.FullName -Destination $Destination -Force
    }
    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        throw "Deployed TubeDesigner template file is missing: $Destination"
    }
    if ((Get-TemplateFileHash $File.FullName) -ne (Get-TemplateFileHash $Destination)) {
        throw "Deployed TubeDesigner template file differs from source: $Destination"
    }
}

if ($ValidateOnly) {
    $ExtraFile = Get-ChildItem -LiteralPath $TargetDirectory -Recurse -File |
        Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') -and
            -not $sourceFiles.Contains($_.FullName.Substring($TargetDirectory.Length).TrimStart('\')) } |
        Select-Object -First 1
    if ($ExtraFile) { throw "Deployed TubeDesigner template file is obsolete: $($ExtraFile.FullName)" }
    Write-Output "VALIDATED TubeDesigner templates: $($Files.Count) files; $ProductTemplateCount product selection contracts; $TargetDirectory"
    return
}

# The runtime template tree is a deployed mirror, not an overlay. Remove files
# whose source template was deleted, including obsolete Python bytecode.
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
