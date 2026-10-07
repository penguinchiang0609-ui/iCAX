param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$version = "3.12.10"
$archiveName = "python-$version-embeddable-amd64.zip"
$archiveUrl = "https://www.python.org/ftp/python/$version/$archiveName"
$archiveSha256 = "156C7EEA90D58CD7E91A23F28A0056616B13E9F4CF4901B7B99B837B7848C6DA"
$dependencyRoot = Join-Path $repoRoot ".deps\python-$version-embed-amd64"
$archivePath = Join-Path $repoRoot ".deps\$archiveName"
$runtimeRoot = [IO.Path]::GetFullPath($OutputDirectory)
$pythonOutput = Join-Path $runtimeRoot "python"
$templateRuntimeOutput = Join-Path $runtimeRoot "template-python"
$templateRuntimeSource = Join-Path $repoRoot "src\iCAX-Engine\framework\TemplateRuntime\python"

function Get-EmbeddedPythonFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)) }
    finally { $stream.Dispose(); $hasher.Dispose() }
}

# This directory contains only the deployed worker and SDK. Verify the target
# and reject links before mirroring removed source modules out of the runtime.
$runtimePrefix = $runtimeRoot.TrimEnd('\') + '\'
$templatePrefix = [IO.Path]::GetFullPath($templateRuntimeOutput).TrimEnd('\') + '\'
$sourcePrefix = [IO.Path]::GetFullPath($templateRuntimeSource).TrimEnd('\') + '\'
if (-not $templatePrefix.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $templatePrefix.Equals($sourcePrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $sourcePrefix.StartsWith($templatePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Invalid template Python deployment directory: $templateRuntimeOutput"
}
$checkedDirectory = $templateRuntimeOutput
while ($checkedDirectory -and ($checkedDirectory.Equals($runtimeRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $checkedDirectory.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase))) {
    if ((Test-Path -LiteralPath $checkedDirectory) -and
        ((Get-Item -LiteralPath $checkedDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Template Python deployment must not follow directory links: $checkedDirectory"
    }
    $checkedDirectory = Split-Path -Parent $checkedDirectory
}
if (Test-Path -LiteralPath $templateRuntimeOutput) {
    $linkedItem = Get-ChildItem -LiteralPath $templateRuntimeOutput -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
    if ($linkedItem) { throw "Template Python deployment must not follow links: $($linkedItem.FullName)" }
}

if ($ValidateOnly) {
    if (-not (Test-Path -LiteralPath (Join-Path $dependencyRoot 'python312.dll') -PathType Leaf)) {
        throw "Embedded Python dependency cache is missing: $dependencyRoot"
    }
    $expectedFiles = @{}
    foreach ($file in Get-ChildItem -LiteralPath $dependencyRoot -File |
        Where-Object { $_.Extension -notin @('.exe', '.pdb') -and $_.Name -notlike '*._pth' }) {
        $expectedFiles['python\' + $file.Name] = $file.FullName
    }
    foreach ($worker in @('icax_template_worker.py', 'icax_template_embedded.py')) {
        $expectedFiles['template-python\' + $worker] = Join-Path $templateRuntimeSource $worker
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $templateRuntimeSource 'icax_template_sdk') -File |
        Where-Object { $_.Extension -notin @('.pyc', '.pyo') }) {
        $expectedFiles['template-python\icax_template_sdk\' + $file.Name] = $file.FullName
    }
    foreach ($relative in $expectedFiles.Keys) {
        $deployed = Join-Path $runtimeRoot $relative
        if (-not (Test-Path -LiteralPath $deployed -PathType Leaf) -or
            (Get-EmbeddedPythonFileHash $deployed) -ne
            (Get-EmbeddedPythonFileHash $expectedFiles[$relative])) {
            throw "Deployed embedded Python file differs from source or is missing: $deployed"
        }
    }
    foreach ($tree in @($pythonOutput, $templateRuntimeOutput)) {
        foreach ($file in Get-ChildItem -LiteralPath $tree -Recurse -File -Force |
            Where-Object { $_.Extension -notin @('.pyc', '.pyo') }) {
            $relative = $file.FullName.Substring($runtimeRoot.Length).TrimStart('\')
            if (-not $expectedFiles.ContainsKey($relative)) { throw "Deployed embedded Python file is obsolete: $($file.FullName)" }
        }
    }
    Write-Output "Validated embedded Python $version`: $($expectedFiles.Count) files; $runtimeRoot"
    return
}

New-Item -ItemType Directory -Path (Split-Path -Parent $archivePath) -Force | Out-Null
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    $downloadPath = "$archivePath.download"
    Invoke-WebRequest -Uri $archiveUrl -OutFile $downloadPath
    Move-Item -LiteralPath $downloadPath -Destination $archivePath -Force
}

$sha256 = [Security.Cryptography.SHA256]::Create()
$archiveStream = [IO.File]::OpenRead($archivePath)
try {
    $actualSha256 = ([BitConverter]::ToString($sha256.ComputeHash($archiveStream))).Replace("-", "")
}
finally {
    $archiveStream.Dispose()
    $sha256.Dispose()
}
if ($actualSha256 -ne $archiveSha256) {
    throw "Embedded Python archive checksum mismatch: $actualSha256"
}

if (-not (Test-Path -LiteralPath (Join-Path $dependencyRoot "python312.dll") -PathType Leaf)) {
    if (Test-Path -LiteralPath $dependencyRoot) {
        throw "Embedded Python dependency cache is incomplete: $dependencyRoot"
    }
    New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $dependencyRoot
}

New-Item -ItemType Directory -Path $pythonOutput -Force | Out-Null
Get-ChildItem -LiteralPath $dependencyRoot -File |
    Where-Object { $_.Extension -notin @(".exe", ".pdb") -and $_.Name -notlike "*._pth" } |
    Copy-Item -Destination $pythonOutput -Force

New-Item -ItemType Directory -Path $templateRuntimeOutput -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $templateRuntimeSource "icax_template_worker.py") -Destination $templateRuntimeOutput -Force
Copy-Item -LiteralPath (Join-Path $templateRuntimeSource "icax_template_embedded.py") -Destination $templateRuntimeOutput -Force
$sdkOutput = Join-Path $templateRuntimeOutput "icax_template_sdk"
New-Item -ItemType Directory -Path $sdkOutput -Force | Out-Null
$sdkFiles = @(Get-ChildItem -LiteralPath (Join-Path $templateRuntimeSource "icax_template_sdk") -File |
    Where-Object { $_.Extension -notin @('.pyc', '.pyo') })
$sdkFiles | Copy-Item -Destination $sdkOutput -Force
$deployedFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[void]$deployedFiles.Add('icax_template_worker.py')
[void]$deployedFiles.Add('icax_template_embedded.py')
foreach ($sdkFile in $sdkFiles) { [void]$deployedFiles.Add('icax_template_sdk\' + $sdkFile.Name) }
Get-ChildItem -LiteralPath $templateRuntimeOutput -Recurse -File -Force | ForEach-Object {
    $relativePath = $_.FullName.Substring($templateRuntimeOutput.Length).TrimStart('\')
    if (-not $deployedFiles.Contains($relativePath)) { Remove-Item -LiteralPath $_.FullName -Force }
}
Get-ChildItem -LiteralPath $templateRuntimeOutput -Recurse -Directory -Force |
    Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
        if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) { Remove-Item -LiteralPath $_.FullName -Force }
    }

Write-Output "Embedded Python $version prepared at $runtimeRoot"
