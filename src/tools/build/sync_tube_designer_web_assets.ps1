param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$outputPrefix = $outputRoot.TrimEnd('\') + '\'
$relativeTrees = @(
    'apps\tube-designer\webpage',
    'apps\_shared\workbench',
    'iCAX-UI\SDK',
    'iCAX-UI\AppProxy',
    'iCAX-UI\ProductProxy',
    'iCAX-UI\ProjectProxy',
    'iCAX-UI\SceneProxy',
    'iCAX-UI\UI'
)
# SDK/index.mjs imports the proxy modules and UI/html.mjs from sibling trees.
# Preserve their relative URLs so AppShell and the product module load entirely
# from the installation, including the viewport's bundled Three.js dependency.
# Native window branding is read from apps/Branding.Setting, independently of
# the webpage favicon. The product icon is included in the webpage tree.
$relativeFiles = @('apps\Branding.Setting', 'apps\branding\icax.ico')

function Get-AssetHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)) }
    finally { $stream.Dispose(); $hasher.Dispose() }
}

# These destinations contain deployed assets only. Validate every target before
# copying files or removing anything from an existing installation.
foreach ($relativeTree in $relativeTrees) {
    $source = [IO.Path]::GetFullPath((Join-Path $sourceRoot $relativeTree))
    $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $relativeTree))
    $targetPrefix = $target.TrimEnd('\') + '\'
    if (-not (Test-Path -LiteralPath $source -PathType Container) -or
        -not $target.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $source.Equals($target, [StringComparison]::OrdinalIgnoreCase) -or
        $source.StartsWith($targetPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Invalid web asset deployment destination: $target"
    }
    $directory = $target
    while ($directory -and ($directory.Equals($outputRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $directory.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase))) {
        if ((Test-Path -LiteralPath $directory) -and
            ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Web asset deployment must not follow directory links: $directory"
        }
        $directory = Split-Path -Parent $directory
    }
    if (Test-Path -LiteralPath $target) {
        $link = Get-ChildItem -LiteralPath $target -Recurse -Force |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
        if ($link) { throw "Web asset deployment must not follow links: $($link.FullName)" }
    }
}

foreach ($relativeFile in $relativeFiles) {
    $source = [IO.Path]::GetFullPath((Join-Path $sourceRoot $relativeFile))
    $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $relativeFile))
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
        -not $target.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $source.Equals($target, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Invalid branding deployment destination: $target"
    }
    $entry = $target
    while ($entry -and ($entry.Equals($outputRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $entry.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase))) {
        if ((Test-Path -LiteralPath $entry) -and
            ((Get-Item -LiteralPath $entry -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Branding deployment must not follow links: $entry"
        }
        $entry = Split-Path -Parent $entry
    }
}

foreach ($relativeTree in $relativeTrees) {
    $source = [IO.Path]::GetFullPath((Join-Path $sourceRoot $relativeTree))
    $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $relativeTree))
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File)
    $sourceFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length).TrimStart('\')
        [void]$sourceFiles.Add($relative)
        $destination = Join-Path $target $relative
        if (-not $ValidateOnly) {
            [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
            (Get-AssetHash $file.FullName) -ne (Get-AssetHash $destination)) {
            throw "Deployed web asset differs from source or is missing: $destination"
        }
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Missing web asset tree: $target" }
    foreach ($file in Get-ChildItem -LiteralPath $target -Recurse -File -Force) {
        $relative = $file.FullName.Substring($target.Length).TrimStart('\')
        if (-not $sourceFiles.Contains($relative)) {
            if ($ValidateOnly) { throw "Deployed web asset is obsolete: $($file.FullName)" }
            Remove-Item -LiteralPath $file.FullName -Force
        }
    }
    if (-not $ValidateOnly) {
        Get-ChildItem -LiteralPath $target -Recurse -Directory -Force |
            Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
                if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) { Remove-Item -LiteralPath $_.FullName -Force }
            }
    }
    Write-Output "Validated web assets: $($files.Count) files; $target"
}

foreach ($relativeFile in $relativeFiles) {
    $source = Join-Path $sourceRoot $relativeFile
    $destination = Join-Path $outputRoot $relativeFile
    if (-not $ValidateOnly) {
        [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
        (Get-AssetHash $source) -ne (Get-AssetHash $destination)) {
        throw "Deployed branding differs from source or is missing: $destination"
    }
    Write-Output "Validated branding: $destination"
}
