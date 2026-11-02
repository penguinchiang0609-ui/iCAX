param([string]$OutputDirectory, [string]$MSBuild)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'output/tests/tube-stages/native-debug' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $MSBuild) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuild = & $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $MSBuild -or -not (Test-Path -LiteralPath $MSBuild)) { throw 'Visual Studio MSBuild is required.' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$projects = @(
    @('cad', 'src/iCAX-Plugins/cad/OpenCascadeResourceImport/OpenCascadeResourceImport.vcxproj', $false),
    @('recognition', 'src/iCAX-Plugins/geometry/ExtrusionRecognition/ExtrusionRecognition.vcxproj', $false),
    @('probe', 'src/tests/icax-plugins/geometry/ExtrusionRecognition/ExtrusionRecognitionTest/ExtrusionRecognitionTest.vcxproj', $true)
)
foreach ($project in $projects) {
    $intermediate = Join-Path (Split-Path $OutputDirectory) ('obj-' + $project[0] + '-debug')
    & $MSBuild (Join-Path $repository $project[1]) /m:4 /p:Configuration=Debug /p:Platform=x64 /p:BuildProjectReferences=false `
        "/p:SolutionDir=$repository\src\" "/p:OutDir=$OutputDirectory\" "/p:IntDir=$intermediate\" `
        "/p:ForceImportBeforeCppTargets=$PSScriptRoot\TubeStageProbe.targets" "/p:TubeStageProbe=$($project[2].ToString().ToLowerInvariant())" /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $($project[0])" }
}
