$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$packageScript = Join-Path $repoRoot 'src/tools/build/package_tube_designer_release.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($packageScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors.Message -join '; ') }
$functionAst = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-ReleasePackageExcludedDlls' }, $true)
if (-not $functionAst) { throw 'DLL selection function not found.' }
. ([scriptblock]::Create($functionAst.Extent.Text))
$fixtureRoot = Join-Path $repoRoot ('output/tests/installer-20261007/dll-selection-fixtures/' + [Guid]::NewGuid().ToString('N'))
$buildRoot = Join-Path $fixtureRoot 'build'
[IO.Directory]::CreateDirectory($buildRoot) | Out-Null
function Assert-Selection([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Write-FixtureProject([string]$Name, [string]$TargetName, [bool]$Disabled = $false) {
    $targetXml = if ($TargetName) { '<TargetName>' + $TargetName + '</TargetName>' } else { '' }
    @"
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup><ProjectName>$Name</ProjectName>$targetXml</PropertyGroup>
  <PropertyGroup Condition="'`$(Configuration)|`$(Platform)'=='Debug|x64'"><TargetName>${Name}Debug</TargetName><ConfigurationType>Application</ConfigurationType></PropertyGroup>
  <PropertyGroup Condition="'`$(Configuration)|`$(Platform)'=='Release|x64'"><ConfigurationType>DynamicLibrary</ConfigurationType></PropertyGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $fixtureRoot ($Name + '.vcxproj')) -Encoding utf8
    $buildXml = if ($Disabled) { '<Build Project="false" />' } else { '' }
    return '<Project Path="' + $Name + '.vcxproj">' + $buildXml + '</Project>'
}
$projects = @(
    (Write-FixtureProject 'Laser3DCAM' 'CamRuntime'),
    (Write-FixtureProject 'OptionalRenderer' 'RendererRuntime' $true),
    (Write-FixtureProject 'DisabledDefault' '' $true),
    (Write-FixtureProject 'CurrentService' '')
)
('<Solution>' + ($projects -join '') + '</Solution>') | Set-Content -LiteralPath (Join-Path $fixtureRoot 'fixture.slnx') -Encoding utf8
foreach ($name in @('Laser3DCAM.dll', 'CamRuntime.dll', 'OptionalRenderer.dll', 'RendererRuntime.dll', 'DisabledDefault.dll', 'CurrentService.dll', 'ThirdParty.dll')) {
    [IO.File]::WriteAllText((Join-Path $buildRoot $name), 'fixture')
}
$excluded = @(Get-ReleasePackageExcludedDlls -SolutionPath (Join-Path $fixtureRoot 'fixture.slnx') -BuildRoot $buildRoot)
$selected = @(Get-ChildItem -LiteralPath $buildRoot -File -Filter '*.dll' | Where-Object { $_.Name -notin $excluded } | ForEach-Object Name)
Assert-Selection ('CamRuntime.dll' -in $selected -and 'Laser3DCAM.dll' -notin $selected) 'Renamed project must ship only its current target.'
Assert-Selection ('OptionalRenderer.dll' -notin $selected -and 'RendererRuntime.dll' -notin $selected -and 'DisabledDefault.dll' -notin $selected) 'Disabled project outputs must not ship.'
Assert-Selection ('CurrentService.dll' -in $selected) 'Enabled current default target must remain.'
Assert-Selection ('ThirdParty.dll' -in $selected) 'Unrelated third-party DLL must remain.'
Assert-Selection ('Laser3DCAMDebug.dll' -notin $excluded) 'Debug-only property must not determine Release target.'
[IO.File]::Delete((Join-Path $buildRoot 'CamRuntime.dll'))
$missingRejected = $false
try { Get-ReleasePackageExcludedDlls -SolutionPath (Join-Path $fixtureRoot 'fixture.slnx') -BuildRoot $buildRoot | Out-Null }
catch { $missingRejected = $_.Exception.Message -match 'Current Release project output missing: CamRuntime.dll' }
Assert-Selection $missingRejected 'Missing renamed current Release DLL must fail before excluding legacy output.'
$live = @(Get-ReleasePackageExcludedDlls -SolutionPath (Join-Path $repoRoot 'src/iCAX.slnx') -BuildRoot (Join-Path $repoRoot 'src/x64/Release'))
$result = [pscustomobject]@{Passed=$true;Checks=6;FixtureExcluded=$excluded;CurrentSolutionExcluded=$live;FixtureRoot=$fixtureRoot}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repoRoot 'output/tests/installer-20261007/dll-selection-regression.json') -Encoding utf8
$result | ConvertTo-Json -Depth 5
