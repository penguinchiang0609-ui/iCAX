param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$IncludeTests,
    [switch]$SkipApplication
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio C++ build tools are required.' }
$kernel = Join-Path $sourceRoot 'iCAX-Engine/foundation/iPlaneNesting'
if (!(Test-Path -LiteralPath (Join-Path $kernel 'dist/Release/bin/iPlaneNesting.dll'))) {
    & (Join-Path $kernel 'build.ps1') -Configuration Release
    if ($LASTEXITCODE) { throw 'iPlaneNesting build failed.' }
}
function Build-Project([string]$RelativePath, [string[]]$Extra = @()) {
    $project = Join-Path $sourceRoot $RelativePath
    if (!(Test-Path -LiteralPath $project -PathType Leaf)) { throw "Missing project: $project" }
    & $msbuild $project /t:Build /m:4 /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 @Extra
    if ($LASTEXITCODE) { throw "Build failed: $RelativePath" }
}
Build-Project 'iCAX-Plugins/geometry/PlaneNestingServices/PlaneNestingServices.vcxproj'
Build-Project 'iCAX-Plugins/product/iPlaneCAM/iPlaneCAM.vcxproj'
if (!$SkipApplication) {
    # Reuse the framework host, with this product's executable name and resources.
    Build-Project 'iCAX-Application/Application/Application.vcxproj' @('/p:ICAXApplicationTargetName=iPlaneCAM', '/p:ICAXSkipTubeDesignerDeployment=true', '/p:BuildProjectReferences=false')
}
if ($IncludeTests) {
    Build-Project 'tests/icax-plugins/geometry/PlaneNestingServices/PlaneNestingServicesTest.vcxproj'
    Build-Project 'tests/icax-plugins/product/iPlaneCAM/iPlaneCAMTest.vcxproj'
    Build-Project 'tools/testing/iplane_cam_native_bridge/iPlaneCAMNativeBridge.vcxproj'
}
Write-Output "iPlaneCAM $Configuration x64 built at $(Join-Path $sourceRoot "x64/$Configuration")"
