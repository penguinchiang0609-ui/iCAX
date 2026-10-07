param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$NSISCompilerPath,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../x64/Release'),
    [string]$VisualCppRuntimeDirectory,
    [string]$ReleaseDate = (Get-Date -Format 'yyyyMMdd')
)

$ErrorActionPreference = 'Stop'
if ($ReleaseDate -notmatch '^\d{8}$') { throw 'ReleaseDate must use yyyyMMdd.' }
[void][DateTime]::ParseExact($ReleaseDate, 'yyyyMMdd', [Globalization.CultureInfo]::InvariantCulture)

function Assert-NsisLiteral([string]$Value) {
    # NSIS expands $ expressions and quoted tokens. Reject these rather than
    # trying to escape arbitrary file names in generated instruction sources.
    if ($Value -match '["$\r\n\t]') { throw "Unsupported character in installer path: $Value" }
}
function Assert-NoPackageLinks([string]$Root) {
    $ancestor = $Root
    while ($ancestor) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Installer payload must not follow links: $ancestor"
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    $link = Get-ChildItem -LiteralPath $Root -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
    if ($link) { throw "Installer payload must not follow links: $($link.FullName)" }
}

$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\')
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$compiler = (Resolve-Path -LiteralPath $NSISCompilerPath).Path
foreach ($value in @($packageRoot, $outputRoot, $compiler)) { Assert-NsisLiteral $value }
if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) { throw 'PackageDirectory must be a directory.' }
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'NSISCompilerPath must be makensis.exe.' }
if ($outputRoot.Equals($packageRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.StartsWith($packageRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $packageRoot.StartsWith($outputRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.Length -le [IO.Path]::GetPathRoot($outputRoot).Length) {
    throw 'OutputDirectory must be separate from the package directory and must not be a drive root.'
}
Assert-NoPackageLinks $packageRoot

# Validation is read-only: use the formal packaging inventory and source hashes
# before enumerating any payload. Do not package arbitrary build-folder files.
$validation = @{
    BuildDirectory = $BuildDirectory
    OutputDirectory = $packageRoot
    ValidateOnly = $true
}
if ($VisualCppRuntimeDirectory) { $validation.VisualCppRuntimeDirectory = $VisualCppRuntimeDirectory }
& (Join-Path $PSScriptRoot 'package_tube_designer_release.ps1') @validation

$files = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Force |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') } |
    Sort-Object FullName)
if ($files.Count -eq 0) { throw 'The validated package is empty.' }
$installerName = "TubeDesigner-0.1.0-$ReleaseDate-x64-Setup.exe"
$installerPath = Join-Path $outputRoot $installerName
$metadataRoot = Join-Path $outputRoot ('.installer-metadata/TubeDesigner-0.1.0-' + $ReleaseDate + '-x64')
Assert-NsisLiteral $metadataRoot
[IO.Directory]::CreateDirectory($metadataRoot) | Out-Null

$installLines = [Collections.Generic.List[string]]::new()
$uninstallLines = [Collections.Generic.List[string]]::new()
$directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$inventory = [Collections.Generic.List[object]]::new()
$lastDirectory = $null
[long]$payloadBytes = 0
foreach ($file in $files) {
    $relative = $file.FullName.Substring($packageRoot.Length + 1)
    Assert-NsisLiteral $file.FullName
    if ([IO.Path]::IsPathRooted($relative) -or $relative.Split('\') -contains '..') {
        throw "Payload escaped its package directory: $relative"
    }
    if ($relative -ieq 'Uninstall.exe') { throw 'The payload must not contain an existing uninstaller.' }
    $directory = Split-Path -Parent $relative
    $installDirectory = if ($directory) { '$INSTDIR\' + $directory } else { '$INSTDIR' }
    if ($lastDirectory -ne $directory) {
        $installLines.Add('SetOutPath "' + $installDirectory + '"')
        $lastDirectory = $directory
    }
    $installLines.Add('File "' + $file.FullName + '"')
    $uninstallLines.Add('Delete "$INSTDIR\' + $relative + '"')
    $parentDirectory = $directory
    while ($parentDirectory) {
        [void]$directories.Add($parentDirectory)
        $parentDirectory = Split-Path -Parent $parentDirectory
    }
    $payloadBytes += $file.Length
    $inventory.Add([ordered]@{
        path = $relative
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}
foreach ($directory in ($directories | Sort-Object { $_.Length } -Descending)) {
    $uninstallLines.Add('RMDir "$INSTDIR\' + $directory + '"')
}

$utf8 = [Text.UTF8Encoding]::new($false)
$installInclude = Join-Path $metadataRoot 'payload-install.nsh'
$uninstallInclude = Join-Path $metadataRoot 'payload-uninstall.nsh'
$configInclude = Join-Path $metadataRoot 'installer-config.nsh'
[IO.File]::WriteAllLines($installInclude, $installLines, $utf8)
[IO.File]::WriteAllLines($uninstallInclude, $uninstallLines, $utf8)
$configLines = @(
    '!ifndef INSTALLER_OUTPUT',
    ('!define INSTALLER_OUTPUT "' + $installerPath + '"'),
    '!endif',
    ('!define INSTALL_FILES_INCLUDE "' + $installInclude + '"'),
    ('!define UNINSTALL_FILES_INCLUDE "' + $uninstallInclude + '"'),
    ('!define PRODUCT_ICON "' + (Join-Path $packageRoot 'apps/tube-designer/webpage/assets/tube-designer.ico') + '"'),
    ('!define PAYLOAD_SIZE_KB ' + [Math]::Ceiling($payloadBytes / 1024.0).ToString([Globalization.CultureInfo]::InvariantCulture))
)
[IO.File]::WriteAllLines($configInclude, $configLines, $utf8)
[IO.File]::WriteAllText((Join-Path $metadataRoot 'payload-inventory.json'),
    (ConvertTo-Json -Depth 5 -InputObject ([ordered]@{ productVersion = '0.1.0'; releaseDate = $ReleaseDate; files = @($inventory.ToArray()) })), $utf8)

$compilerLog = Join-Path $metadataRoot 'makensis.log'
& $compiler '/INPUTCHARSET' 'UTF8' ('/DCONFIG_INCLUDE=' + $configInclude) (Join-Path $PSScriptRoot 'TubeDesigner.nsi') 2>&1 |
    Tee-Object -FilePath $compilerLog
if ($LASTEXITCODE -ne 0) { throw "NSIS compilation failed with exit code $LASTEXITCODE. See $compilerLog" }
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) { throw "NSIS did not produce the installer: $installerPath" }
$installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($installerPath + '.sha256'), "$installerHash  $installerName`r`n", $utf8)
Write-Output "Created installer: $installerPath"
Write-Output "Payload: $($files.Count) files; SHA256: $installerHash"
