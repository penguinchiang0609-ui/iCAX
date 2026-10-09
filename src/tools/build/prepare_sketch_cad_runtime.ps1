[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$CacheDirectory,
    [switch]$Download,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$taskDependencyRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../third_party/libredwg'))
$taskMetadata = Get-Content -LiteralPath (Join-Path $taskDependencyRoot 'libredwg.json') -Raw | ConvertFrom-Json
$taskCache = if ([string]::IsNullOrWhiteSpace($CacheDirectory)) { Join-Path $taskDependencyRoot 'cache' } else { [System.IO.Path]::GetFullPath($CacheDirectory) }
$taskOutputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$taskRuntimeRoot = Join-Path $taskOutputRoot $taskMetadata.runtimeDirectory

function Assert-TaskHash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing CAD converter file: $Path. Run prepare_sketch_cad_runtime.ps1 -OutputDirectory <binary-root> -Download." }
    $taskHashAlgorithm = [System.Security.Cryptography.SHA256]::Create()
    $taskStream = [System.IO.File]::OpenRead($Path)
    try { $taskHash = [System.BitConverter]::ToString($taskHashAlgorithm.ComputeHash($taskStream)).Replace('-','') }
    finally { $taskStream.Dispose(); $taskHashAlgorithm.Dispose() }
    if ($taskHash -ne $Expected) { throw "CAD converter SHA256 mismatch: $Path" }
}
function Resolve-TaskChild([string]$Root, [string]$Relative) {
    if ([System.IO.Path]::IsPathRooted($Relative)) { throw 'Manifest paths must be relative.' }
    $taskResolved = [System.IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $taskResolved.StartsWith($Root.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escaped its directory.' }
    return $taskResolved
}

if ($Download -and $ValidateOnly) { throw '-Download and -ValidateOnly cannot be combined.' }
if ($Download) {
    $taskArchiveRoot = Join-Path $taskCache 'archives'
    New-Item -ItemType Directory -Force -Path $taskArchiveRoot | Out-Null
    foreach ($taskArtifact in $taskMetadata.artifacts) {
        $taskArchive = Resolve-TaskChild $taskArchiveRoot $taskArtifact.name
        if (-not (Test-Path -LiteralPath $taskArchive -PathType Leaf)) {
            $taskTemporary = $taskArchive + '.download'
            try {
                $taskFetched = $false
                for ($taskAttempt = 1; $taskAttempt -le 3; $taskAttempt++) {
                    try { Invoke-WebRequest -Uri $taskArtifact.url -OutFile $taskTemporary -UseBasicParsing; $taskFetched = $true; break }
                    catch { if ($taskAttempt -eq 3) { throw }; Start-Sleep -Seconds 2 }
                }
                if (-not $taskFetched) { throw 'CAD converter download failed.' }
                Assert-TaskHash $taskTemporary $taskArtifact.sha256
                Move-Item -LiteralPath $taskTemporary -Destination $taskArchive
            } finally { if (Test-Path -LiteralPath $taskTemporary) { Remove-Item -LiteralPath $taskTemporary -Force } }
        }
        Assert-TaskHash $taskArchive $taskArtifact.sha256
    }
    $taskWindowsRoot = Join-Path $taskCache 'windows'
    $taskSourcesRoot = Join-Path $taskCache 'sources'
    New-Item -ItemType Directory -Force -Path $taskWindowsRoot,$taskSourcesRoot | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $taskZip = [System.IO.Compression.ZipFile]::OpenRead((Join-Path $taskArchiveRoot "libredwg-$($taskMetadata.version)-win64.zip"))
    try {
        foreach ($taskBinary in @('dwg2dxf.exe','libredwg-0.dll','libiconv-2.dll')) {
            $taskEntry = $taskZip.GetEntry($taskBinary)
            if ($null -eq $taskEntry) { throw "Missing pinned archive entry: $taskBinary" }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($taskEntry, (Join-Path $taskWindowsRoot $taskBinary), $true)
        }
    } finally { $taskZip.Dispose() }
    foreach ($taskSourceArchive in @("libredwg-$($taskMetadata.version).tar.xz",'libiconv-1.18.tar.gz')) {
        & tar.exe -xf (Join-Path $taskArchiveRoot $taskSourceArchive) -C $taskSourcesRoot
        if ($LASTEXITCODE -ne 0) { throw "Cannot unpack corresponding source: $taskSourceArchive" }
    }
}

$taskExpectedFiles = @{}
foreach ($taskFile in $taskMetadata.deployFiles) {
    $taskTarget = Resolve-TaskChild $taskRuntimeRoot $taskFile.relative
    $taskExpectedFiles[$taskTarget.ToLowerInvariant()] = $true
    if (-not $ValidateOnly) {
        $taskSource = Resolve-TaskChild $taskCache $taskFile.source
        Assert-TaskHash $taskSource $taskFile.sha256
        New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($taskTarget)) | Out-Null
        Copy-Item -LiteralPath $taskSource -Destination $taskTarget -Force
    }
    Assert-TaskHash $taskTarget $taskFile.sha256
}
foreach ($taskActual in Get-ChildItem -LiteralPath $taskRuntimeRoot -File -Recurse) {
    if (-not $taskExpectedFiles.ContainsKey($taskActual.FullName.ToLowerInvariant())) { throw "Unexpected file in controlled CAD runtime: $($taskActual.FullName)" }
}
Write-Host "Verified CAD runtime $($taskMetadata.version): $($taskExpectedFiles.Count) allowlisted files."
