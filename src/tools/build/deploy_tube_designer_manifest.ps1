param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$sourceManifest = Join-Path $sourceRoot 'apps/tube-designer/product.manifest.json'
$targetManifest = Join-Path $outputRoot 'apps/tube-designer/product.manifest.json'
if ([IO.Path]::GetFullPath($sourceManifest) -eq [IO.Path]::GetFullPath($targetManifest)) {
    throw 'The deployment directory must not be the source directory.'
}

# Source manifests resolve DLLs from src/apps/<product>. Installed manifests
# resolve from <binary-dir>/apps/<product>; copying the source verbatim doubles
# the platform/configuration directories. Preserve all catalogue data verbatim.
$sourceText = [IO.File]::ReadAllText($sourceManifest)
$deployedText = $sourceText.Replace('../../${Platform}/${Configuration}/', '../../')
$manifest = $deployedText | ConvertFrom-Json
$modulePaths = @($manifest.backend.modules.PSObject.Properties | ForEach-Object { $_.Value })
$modulePaths += @($manifest.backend.resources.handlers | ForEach-Object { $_.module })
$productRoot = Split-Path -Parent $targetManifest
foreach ($modulePath in ($modulePaths | Sort-Object -Unique)) {
    if (!$modulePath) {
        throw "Unresolved module path: $modulePath"
    }
    # Runtime manifests may deliberately anchor native modules at the binary
    # directory.  Expand that known token only for deployment validation; keep
    # the token in the emitted manifest so installation remains relocatable.
    $validationPath = $modulePath.Replace('${ExecutableDirectory}', $outputRoot)
    if ($validationPath.Contains('${')) {
        throw "Unresolved module path: $modulePath"
    }
    $resolvedModule = if ([IO.Path]::IsPathRooted($validationPath)) {
        [IO.Path]::GetFullPath($validationPath)
    } else {
        [IO.Path]::GetFullPath((Join-Path $productRoot $validationPath))
    }
    if (!(Test-Path -LiteralPath $resolvedModule -PathType Leaf)) {
        throw "Deployment aborted: required module missing: $resolvedModule"
    }
}

if (!$ValidateOnly) {
    [IO.Directory]::CreateDirectory($productRoot) | Out-Null
    [IO.File]::WriteAllText($targetManifest, $deployedText, [Text.UTF8Encoding]::new($false))
}
Write-Output "Validated $(@($modulePaths | Sort-Object -Unique).Count) module paths; manifest: $targetManifest"
