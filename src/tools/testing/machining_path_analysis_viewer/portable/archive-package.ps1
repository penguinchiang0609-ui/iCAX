param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$ZipPath,
    [Parameter(Mandatory=$true)][string]$ExtractDirectory
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$packagePath = (Resolve-Path -LiteralPath $PackageDirectory).Path
$zipFullPath = [System.IO.Path]::GetFullPath($ZipPath)
$extractFullPath = [System.IO.Path]::GetFullPath($ExtractDirectory)
if (Test-Path -LiteralPath $zipFullPath) { throw "ZIP already exists: $zipFullPath" }
if (Test-Path -LiteralPath $extractFullPath) { throw "Extract directory already exists: $extractFullPath" }
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($zipFullPath)) | Out-Null
$stream = [System.IO.File]::Open($zipFullPath, [System.IO.FileMode]::CreateNew)
try {
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create, $false, [System.Text.Encoding]::UTF8)
    try {
        $allFiles = @(Get-ChildItem -LiteralPath $packagePath -File -Recurse)
        $count = 0
        foreach ($file in $allFiles) {
            $entryName = $file.FullName.Substring($packagePath.Length + 1).Replace('\','/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            $count++
            if (($count % 1000) -eq 0) { Write-Output "Archived $count / $($allFiles.Count) files" }
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }
[System.IO.Compression.ZipFile]::ExtractToDirectory($zipFullPath, $extractFullPath, [System.Text.Encoding]::UTF8)
$archiveItem = Get-Item -LiteralPath $zipFullPath
[pscustomobject]@{zip=$zipFullPath;bytes=$archiveItem.Length;sha256=(Get-FileHash -LiteralPath $zipFullPath -Algorithm SHA256).Hash.ToLower();extracted=$extractFullPath} | ConvertTo-Json
