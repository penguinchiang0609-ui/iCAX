$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$SyncScript = Join-Path $RepoRoot 'src\tools\build\sync_tube_designer_templates.ps1'
$SourceDirectory = Join-Path $RepoRoot 'src\apps\tube-designer\templates'
$ArtifactsDirectory = Join-Path $RepoRoot 'artifacts'
$RunDirectory = [IO.Path]::GetFullPath((Join-Path $ArtifactsDirectory (
    'template-deployment-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))))
$ArtifactsPrefix = [IO.Path]::GetFullPath($ArtifactsDirectory).TrimEnd('\') + '\'
if (-not $RunDirectory.StartsWith($ArtifactsPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Test directory must stay inside repository artifacts: $RunDirectory"
}
New-Item -ItemType Directory -Path $RunDirectory | Out-Null
$RuntimeDirectory = Join-Path $RunDirectory 'runtime'
New-Item -ItemType Directory -Path $RuntimeDirectory | Out-Null
$TargetDirectory = Join-Path $RuntimeDirectory 'apps\tube-designer\templates'
$Passed = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Complete-Test {
    param([string]$Name)
    ++$script:Passed
    Write-Output "PASS $Name"
}

function Get-RegressionFileHash {
    param([string]$Path)
    $Hasher = [Security.Cryptography.SHA256]::Create()
    $Stream = [IO.File]::OpenRead($Path)
    try {
        return [BitConverter]::ToString($Hasher.ComputeHash($Stream)).Replace('-', '')
    }
    finally {
        $Stream.Dispose()
        $Hasher.Dispose()
    }
}

function Get-TargetSnapshot {
    $Snapshot = @{}
    foreach ($File in Get-ChildItem -LiteralPath $TargetDirectory -Recurse -File -Force) {
        $Relative = $File.FullName.Substring($TargetDirectory.Length).TrimStart('\')
        $Snapshot[$Relative] = "$($File.Length):$($File.LastWriteTimeUtc.Ticks):$(Get-RegressionFileHash $File.FullName)"
    }
    return ,$Snapshot
}

function Assert-SnapshotUnchanged {
    param([hashtable]$Before, [string]$Context)
    $After = Get-TargetSnapshot
    Assert-True ($Before.Count -eq $After.Count) "$Context changed the deployed file count."
    foreach ($Relative in $Before.Keys) {
        Assert-True ($After.ContainsKey($Relative) -and $After[$Relative] -eq $Before[$Relative]) (
            "$Context modified deployed file: $Relative")
    }
}

function Invoke-ValidateOnly {
    $Before = Get-TargetSnapshot
    $Output = @(& $SyncScript -OutputDirectory $RuntimeDirectory -ValidateOnly)
    Assert-True (($Output -join "`n") -match '^VALIDATED TubeDesigner templates:') 'ValidateOnly did not report successful validation.'
    Assert-SnapshotUnchanged $Before 'Successful ValidateOnly'
}

function Assert-ValidationFails {
    param([string]$ExpectedMessage, [string]$ExpectedPath)
    $Before = Get-TargetSnapshot
    $Failure = $null
    try {
        & $SyncScript -OutputDirectory $RuntimeDirectory -ValidateOnly | Out-Null
    }
    catch {
        $Failure = $_.Exception.Message
    }
    Assert-True ($null -ne $Failure) 'ValidateOnly unexpectedly accepted a broken deployment.'
    Assert-True ($Failure.Contains($ExpectedMessage) -and $Failure.Contains($ExpectedPath)) (
        "ValidateOnly did not identify the broken file. Actual error: $Failure")
    Assert-SnapshotUnchanged $Before 'Failed ValidateOnly'
}

# Use the real source catalogue and real deployment command in a fresh runtime.
# The test never changes source templates or either installed runtime directory.
& $SyncScript -OutputDirectory $RuntimeDirectory
$SourceFiles = @(Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') })
$TargetFiles = @(Get-ChildItem -LiteralPath $TargetDirectory -Recurse -File -Force)
Assert-True ($SourceFiles.Count -eq $TargetFiles.Count) 'Initial sync did not create an exact file mirror.'
$ProductTemplates = 0
foreach ($Manifest in Get-ChildItem -LiteralPath (Join-Path $SourceDirectory 'assembly') -Recurse -Filter assembly.json -File) {
    $Descriptor = [IO.File]::ReadAllText($Manifest.FullName) | ConvertFrom-Json
    Assert-True ($null -ne $Descriptor.inputContract -and $null -ne $Descriptor.exampleInput) (
        "Assembly template '$($Descriptor.id)' is missing its current selection contract.")
    ++$ProductTemplates
    $RelativeDirectory = $Manifest.DirectoryName.Substring($SourceDirectory.Length).TrimStart('\')
    foreach ($Hook in @('applicability.py', 'example.py')) {
        Assert-True (Test-Path -LiteralPath (Join-Path $TargetDirectory "$RelativeDirectory\$Hook") -PathType Leaf) (
            "Deployed product template '$($Descriptor.id)' is missing $Hook.")
    }
}
Assert-True ($ProductTemplates -gt 0) 'The deployment regression exercised no product selection contracts.'
Complete-Test "Initial sync copied all $($SourceFiles.Count) source files and $ProductTemplates product selection contracts"

Invoke-ValidateOnly
Complete-Test 'ValidateOnly accepts the complete deployment without writing it'

$ApplicabilityPath = Join-Path $TargetDirectory 'assembly\bend\applicability.py'
Remove-Item -LiteralPath $ApplicabilityPath
Assert-ValidationFails 'Deployed TubeDesigner template file is missing:' $ApplicabilityPath
Assert-True (-not (Test-Path -LiteralPath $ApplicabilityPath)) 'ValidateOnly silently repaired the missing applicability hook.'
Complete-Test 'Missing bend applicability hook fails validation with its exact path and is not repaired'

& $SyncScript -OutputDirectory $RuntimeDirectory
Invoke-ValidateOnly
Complete-Test 'Sync repairs a missing hook and the repaired deployment validates'

$HelperPath = Join-Path $TargetDirectory '_shared\assembly_example_product.py'
$HelperBytes = [IO.File]::ReadAllBytes($HelperPath)
Assert-True ($HelperBytes.Length -gt 0) 'Example product helper is unexpectedly empty.'
$OriginalLength = $HelperBytes.Length
$OriginalHash = Get-RegressionFileHash $HelperPath
$HelperBytes[0] = $HelperBytes[0] -bxor 1
[IO.File]::WriteAllBytes($HelperPath, $HelperBytes)
Assert-True ((Get-Item -LiteralPath $HelperPath).Length -eq $OriginalLength) 'Wrong-version fixture must preserve file length.'
Assert-True ((Get-RegressionFileHash $HelperPath) -ne $OriginalHash) 'Wrong-version fixture failed to change the helper.'
Assert-ValidationFails 'Deployed TubeDesigner template file differs from source:' $HelperPath
Complete-Test 'Same-length wrong-version shared helper fails SHA256 validation without being overwritten'

& $SyncScript -OutputDirectory $RuntimeDirectory
Invoke-ValidateOnly
Assert-True ((Get-RegressionFileHash $HelperPath) -eq $OriginalHash) 'Sync failed to repair the wrong-version helper.'

$ObsoleteDirectory = Join-Path $TargetDirectory 'assembly\obsolete-deployment-test'
$CacheDirectory = Join-Path $TargetDirectory 'assembly\bend\__pycache__'
New-Item -ItemType Directory -Path $ObsoleteDirectory, $CacheDirectory -Force | Out-Null
$ObsoletePath = Join-Path $ObsoleteDirectory 'assembly.json'
[IO.File]::WriteAllText($ObsoletePath, '{"id":"obsolete-deployment-test"}')
[IO.File]::WriteAllBytes((Join-Path $CacheDirectory 'applicability.cpython-test.pyc'), [byte[]](1, 2, 3))
[IO.File]::WriteAllBytes((Join-Path $TargetDirectory '_shared\obsolete.pyc'), [byte[]](4, 5, 6))
[IO.File]::WriteAllBytes((Join-Path $TargetDirectory '_shared\obsolete.pyo'), [byte[]](7, 8, 9))
Assert-ValidationFails 'Deployed TubeDesigner template file is obsolete:' $ObsoletePath
Complete-Test 'An obsolete runtime template fails validation without being removed'

& $SyncScript -OutputDirectory $RuntimeDirectory
Invoke-ValidateOnly
Assert-True (-not (Test-Path -LiteralPath $ObsoleteDirectory)) 'Sync retained an obsolete template directory.'
Complete-Test 'Sync removes obsolete template files and empty directories after repairing wrong versions'

$DeployedCaches = @(Get-ChildItem -LiteralPath $TargetDirectory -Recurse -Force |
    Where-Object { $_.Name -eq '__pycache__' -or $_.Extension -in @('.pyc', '.pyo') })
Assert-True ($DeployedCaches.Count -eq 0) 'Python cache directories or bytecode were deployed or retained.'
Complete-Test 'Source Python caches are excluded and stale runtime bytecode is removed'

# Parse the build target separately; runtime mirror behavior above remains the
# main regression rather than testing a copied implementation as a string.
[xml]$BuildTargets = [IO.File]::ReadAllText((Join-Path $RepoRoot 'src\Directory.Build.targets'))
$CopyTargets = @($BuildTargets.SelectNodes("//*[local-name()='Target' and @Name='ICAXCopyTubeDesignerTemplates']"))
Assert-True ($CopyTargets.Count -eq 1) 'Expected one TubeDesigner template deployment build target.'
$CopyTarget = $CopyTargets[0]
Assert-True ($CopyTarget.AfterTargets -eq 'Build') 'Template deployment is not attached to Build.'
foreach ($Project in @('TubeDesigner', 'Application')) {
    Assert-True ($CopyTarget.Condition -match ("'\$\(MSBuildProjectName\)'\s*==\s*'" + $Project + "'")) (
        "Template deployment build target does not include $Project.")
}
Assert-True ($CopyTarget.Condition -match "'\$\(DesignTimeBuild\)'\s*!=\s*'true'") 'Template deployment is not excluded from design-time builds.'
$ExecNodes = @($CopyTarget.SelectNodes("*[local-name()='Exec']"))
Assert-True ($ExecNodes.Count -eq 1 -and $ExecNodes[0].Command.Contains('sync_tube_designer_templates.ps1')) (
    'Template deployment build target does not invoke the tested sync command.')
Complete-Test 'TubeDesigner and Application builds invoke template sync after Build'

Write-Output "TubeDesigner template deployment regression: $Passed tests passed."
Write-Output "Test runtime retained for inspection: $RuntimeDirectory"
