param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$VisualCppRuntimeDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
function Get-ReleasePackageFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)) }
    finally { $stream.Dispose(); $hasher.Dispose() }
}
function Get-ReleasePackageExcludedDlls([string]$SolutionPath, [string]$BuildRoot) {
    # A shared build directory can retain old target names and outputs of
    # disabled projects. Derive these exclusions from the current solution;
    # unrelated third-party DLLs are still collected below.
    function Test-ReleaseProjectCondition([string]$Condition) {
        if (-not $Condition) { return $true }
        $expanded = $Condition.Replace('$(Configuration)', 'Release').Replace('$(Platform)', 'x64')
        if ($expanded -match '^\s*([''"])(.*?)\1\s*==\s*([''"])(.*?)\3\s*$') {
            return $Matches[2].Equals($Matches[4], [StringComparison]::OrdinalIgnoreCase)
        }
        throw "Unsupported condition on project output properties: $Condition"
    }
    [xml]$solution = Get-Content -LiteralPath $SolutionPath -Raw
    $solutionRoot = Split-Path -Parent $SolutionPath
    $excluded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $enabledOutputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($solutionProject in $solution.SelectNodes('//Project[@Path]')) {
        if ([IO.Path]::GetExtension($solutionProject.Path) -ne '.vcxproj') { continue }
        $projectPath = Join-Path $solutionRoot $solutionProject.Path
        [xml]$project = Get-Content -LiteralPath $projectPath -Raw
        $projectName = [IO.Path]::GetFileNameWithoutExtension($projectPath)
        $targetName = $projectName
        $hasTargetName = $false
        $configurationType = ''
        foreach ($group in $project.Project.PropertyGroup) {
            $outputProperties = @($group.ChildNodes | Where-Object { $_.LocalName -in @('ProjectName', 'TargetName', 'ConfigurationType') })
            if (-not $outputProperties.Count -or -not (Test-ReleaseProjectCondition $group.GetAttribute('Condition'))) { continue }
            foreach ($property in $outputProperties) {
                if (-not (Test-ReleaseProjectCondition $property.GetAttribute('Condition'))) { continue }
                $value = $property.InnerText.Trim().Replace('$(Configuration)', 'Release').Replace('$(Platform)', 'x64').Replace('$(MSBuildProjectName)', [IO.Path]::GetFileNameWithoutExtension($projectPath)).Replace('$(ProjectName)', $projectName)
                if ($value.Contains('$(')) { throw "Unresolved project output property in $projectPath`: $value" }
                switch ($property.LocalName) {
                    ProjectName { $projectName = $value }
                    TargetName { $targetName = $value; $hasTargetName = $true }
                    ConfigurationType { $configurationType = $value }
                }
            }
        }
        if (-not $hasTargetName) { $targetName = $projectName }
        if ($configurationType -ne 'DynamicLibrary') { continue }
        $currentDll = $targetName + '.dll'
        $oldDll = $projectName + '.dll'
        $disabled = @($solutionProject.SelectNodes('./Build[@Project="false"]')).Count -gt 0
        if ($disabled) {
            [void]$excluded.Add($currentDll)
            [void]$excluded.Add($oldDll)
        } else {
            [void]$enabledOutputs.Add($currentDll)
            if (-not $currentDll.Equals($oldDll, [StringComparison]::OrdinalIgnoreCase)) {
                if (-not (Test-Path -LiteralPath (Join-Path $BuildRoot $currentDll) -PathType Leaf)) {
                    throw "Current Release project output missing: $currentDll ($projectPath)"
                }
                [void]$excluded.Add($oldDll)
            }
        }
    }
    # A name reused by another enabled project is a current output.
    foreach ($name in $enabledOutputs) { [void]$excluded.Remove($name) }
    return @($excluded | Sort-Object)
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$outputPrefix = $outputRoot + '\'
if ($outputRoot.Equals($buildRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $buildRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $sourceRoot.Equals($outputRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $sourceRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.Length -le [IO.Path]::GetPathRoot($outputRoot).Length) {
    throw 'The package directory must be separate from the build and source directories.'
}
$checked = $outputRoot
while ($checked) {
    if ((Test-Path -LiteralPath $checked) -and
        ((Get-Item -LiteralPath $checked -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Package deployment must not follow links: $checked"
    }
    $checked = Split-Path -Parent $checked
}
if (Test-Path -LiteralPath $outputRoot) {
    $link = Get-ChildItem -LiteralPath $outputRoot -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
    if ($link) { throw "Package deployment must not follow links: $($link.FullName)" }
}

$executable = Join-Path $buildRoot 'TubeDesigner.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or
    (Get-Item -LiteralPath $executable).VersionInfo.IsDebug) {
    throw "A Release TubeDesigner.exe is required: $executable"
}
& (Join-Path $PSScriptRoot 'validate_tube_designer_licensing.ps1') -BuildDirectory $buildRoot
if (-not $VisualCppRuntimeDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
        if ($installation) {
            $redistRoot = Join-Path $installation 'VC/Redist/MSVC'
            $version = Get-ChildItem -LiteralPath $redistRoot -Directory |
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
            if ($version) { $VisualCppRuntimeDirectory = Join-Path $version.FullName 'x64/Microsoft.VC143.CRT' }
        }
    }
}
if (-not $VisualCppRuntimeDirectory -or -not (Test-Path -LiteralPath $VisualCppRuntimeDirectory -PathType Container)) {
    throw 'Supply the x64 Visual C++ redistributable directory with -VisualCppRuntimeDirectory.'
}
$crtFiles = @(Get-ChildItem -LiteralPath $VisualCppRuntimeDirectory -File -Filter '*.dll')
foreach ($required in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if ($required -notin $crtFiles.Name) { throw "Required x64 Visual C++ runtime DLL missing: $required" }
}

# Ship only production binaries and CEF data. Tests, symbols, intermediate
# files, log files and user data from the build directory are never copied.
$nativeFiles = @{'TubeDesigner.exe' = $executable}
$excludedProjectDlls = @(Get-ReleasePackageExcludedDlls -SolutionPath (Join-Path $sourceRoot 'iCAX.slnx') -BuildRoot $buildRoot)
Write-Output "Excluded obsolete or disabled project DLLs: $($excludedProjectDlls -join ', ')"
foreach ($file in Get-ChildItem -LiteralPath $buildRoot -File -Filter '*.dll') {
    if ($file.Name -in $excludedProjectDlls) { continue }
    $nativeFiles[$file.Name] = $file.FullName
}
foreach ($file in $crtFiles) { $nativeFiles[$file.Name] = $file.FullName }
foreach ($required in @('chrome_100_percent.pak', 'chrome_200_percent.pak', 'resources.pak',
    'icudtl.dat', 'v8_context_snapshot.bin', 'vk_swiftshader_icd.json')) {
    $file = Join-Path $buildRoot $required
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required CEF runtime data missing: $file" }
    $nativeFiles[$required] = $file
}
$localeRoot = Join-Path $buildRoot 'locales'
if (-not (Test-Path -LiteralPath $localeRoot -PathType Container)) { throw "CEF locales missing: $localeRoot" }
foreach ($file in Get-ChildItem -LiteralPath $localeRoot -File -Filter '*.pak') {
    $nativeFiles['locales\' + $file.Name] = $file.FullName
}
if (-not $ValidateOnly) { [IO.Directory]::CreateDirectory($outputRoot) | Out-Null }
foreach ($relative in $nativeFiles.Keys) {
    $destination = Join-Path $outputRoot $relative
    if (-not $ValidateOnly) {
        [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
        Copy-Item -LiteralPath $nativeFiles[$relative] -Destination $destination -Force
    }
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
        (Get-ReleasePackageFileHash $destination) -ne
        (Get-ReleasePackageFileHash $nativeFiles[$relative])) {
        throw "Packaged native binary or CEF data differs from source or is missing: $destination"
    }
}

& (Join-Path $PSScriptRoot 'deploy_tube_designer_manifest.ps1') -OutputDirectory $outputRoot -ValidateOnly:$ValidateOnly
& (Join-Path $PSScriptRoot 'sync_tube_designer_templates.ps1') -OutputDirectory $outputRoot -ValidateOnly:$ValidateOnly
& (Join-Path $PSScriptRoot 'sync_tube_designer_web_assets.ps1') -OutputDirectory $outputRoot -ValidateOnly:$ValidateOnly
& (Join-Path $PSScriptRoot 'prepare_embedded_python_runtime.ps1') -OutputDirectory (Join-Path $outputRoot 'runtime') -ValidateOnly:$ValidateOnly
if (-not $ValidateOnly) {
    & (Join-Path $PSScriptRoot 'prepare_embedded_python_runtime.ps1') -OutputDirectory (Join-Path $outputRoot 'runtime') -ValidateOnly
}

# Check the entire package inventory too, including folders outside the trees
# managed by the individual deployment scripts. Never remove unknown user data.
$expectedFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $nativeFiles.Keys) { [void]$expectedFiles.Add($relative) }
foreach ($relative in @('apps\Branding.Setting', 'apps\branding\icax.ico', 'apps\tube-designer\product.manifest.json')) {
    [void]$expectedFiles.Add($relative)
}
foreach ($tree in @('apps\tube-designer\webpage', 'apps\tube-designer\templates', 'apps\_shared\workbench',
    'iCAX-UI\SDK', 'iCAX-UI\AppProxy', 'iCAX-UI\ProductProxy', 'iCAX-UI\ProjectProxy', 'iCAX-UI\SceneProxy', 'iCAX-UI\UI')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot $tree) -Recurse -File |
        Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') }) {
        [void]$expectedFiles.Add($file.FullName.Substring($sourceRoot.Length).TrimStart('\'))
    }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $outputRoot 'runtime\python') -File) {
    [void]$expectedFiles.Add('runtime\python\' + $file.Name)
}
foreach ($relative in @('icax_template_worker.py', 'icax_template_embedded.py')) {
    [void]$expectedFiles.Add('runtime\template-python\' + $relative)
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'iCAX-Engine\framework\TemplateRuntime\python\icax_template_sdk') -File |
    Where-Object { $_.Extension -notin @('.pyc', '.pyo') }) {
    [void]$expectedFiles.Add('runtime\template-python\icax_template_sdk\' + $file.Name)
}
foreach ($file in Get-ChildItem -LiteralPath $outputRoot -Recurse -File -Force |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo') }) {
    $relative = $file.FullName.Substring($outputRoot.Length).TrimStart('\')
    if (-not $expectedFiles.Contains($relative)) { throw "Unexpected file in the release package: $($file.FullName)" }
}
Write-Output "Validated Release package: $($nativeFiles.Count) native/CEF files, $($crtFiles.Count) x64 VC runtime DLLs; $outputRoot"
