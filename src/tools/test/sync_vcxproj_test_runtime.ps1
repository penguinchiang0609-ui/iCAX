param(
    [Parameter(Mandatory = $true)]
    [string]$TestProject,

    [Parameter(Mandatory = $true)]
    [string]$OutDir,

    [Parameter(Mandatory = $true)]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$Platform
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Read-ProjectXml {
    param([Parameter(Mandatory = $true)][string]$ProjectPath)

    [xml]$xml = Get-Content -LiteralPath $ProjectPath -Raw -Encoding UTF8
    return $xml
}

function Get-ProjectName {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)]$ProjectXml
    )

    $ns = New-Object System.Xml.XmlNamespaceManager($ProjectXml.NameTable)
    $ns.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

    $projectNameNode = $ProjectXml.SelectSingleNode("//msb:PropertyGroup[@Label='Globals']/msb:ProjectName", $ns)
    if ($projectNameNode -and -not [string]::IsNullOrWhiteSpace($projectNameNode.InnerText)) {
        return $projectNameNode.InnerText.Trim()
    }

    return [System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)
}

function Get-ProjectReferences {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)]$ProjectXml
    )

    $ns = New-Object System.Xml.XmlNamespaceManager($ProjectXml.NameTable)
    $ns.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

    $projectDir = Split-Path -Parent $ProjectPath
    $nodes = $ProjectXml.SelectNodes("//msb:ProjectReference", $ns)
    $references = @()

    foreach ($node in $nodes) {
        $include = $node.Include
        if ([string]::IsNullOrWhiteSpace($include)) {
            continue
        }

        $references += Resolve-FullPath (Join-Path $projectDir $include)
    }

    return $references
}

function Sync-ReferencedProject {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)][string]$DestinationDir,
        [Parameter(Mandatory = $true)]$Visited
    )

    $fullProjectPath = Resolve-FullPath $ProjectPath
    if ($Visited.ContainsKey($fullProjectPath)) {
        return
    }
    $Visited[$fullProjectPath] = $true

    if (-not (Test-Path -LiteralPath $fullProjectPath)) {
        throw "Referenced project does not exist: $fullProjectPath"
    }

    $xml = Read-ProjectXml $fullProjectPath
    foreach ($reference in Get-ProjectReferences $fullProjectPath $xml) {
        Sync-ReferencedProject $reference $DestinationDir $Visited
    }

    $projectName = Get-ProjectName $fullProjectPath $xml
    $projectDir = Split-Path -Parent $fullProjectPath
    $sourceRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..")
    $sharedDllPath = Join-Path $sourceRoot (Join-Path $Platform (Join-Path $Configuration "$projectName.dll"))
    $projectDllPath = Join-Path $projectDir (Join-Path $Platform (Join-Path $Configuration (Join-Path $projectName "$projectName.dll")))
    $dllPath = if (Test-Path -LiteralPath $sharedDllPath) { $sharedDllPath } else { $projectDllPath }

    if (Test-Path -LiteralPath $dllPath) {
        Copy-Item -LiteralPath $dllPath -Destination $DestinationDir -Force
    }
}

$resolvedTestProject = Resolve-FullPath $TestProject
$resolvedOutDir = Resolve-FullPath $OutDir
New-Item -ItemType Directory -Path $resolvedOutDir -Force | Out-Null

$visited = @{}
$testXml = Read-ProjectXml $resolvedTestProject
foreach ($reference in Get-ProjectReferences $resolvedTestProject $testXml) {
    Sync-ReferencedProject $reference $resolvedOutDir $visited
}
