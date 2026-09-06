param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
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
Get-ChildItem -LiteralPath (Join-Path $templateRuntimeSource "icax_template_sdk") -File |
    Copy-Item -Destination $sdkOutput -Force

Write-Output "Embedded Python $version prepared at $runtimeRoot"
