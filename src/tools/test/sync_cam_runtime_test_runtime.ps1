param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutDir,

    [string]$Configuration = "Debug",

    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath $RepoRoot).Path
$Destination = (Resolve-Path -LiteralPath $OutDir).Path
$SharedRuntimeDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $Root ("{0}\{1}" -f $Platform, $Configuration)))

# ProjectReference builds each dependency into the caller's OutDir. Preserve
# those exact binaries in isolated builds; a shared runtime may be older or be
# built with different flags. Shared files only fill missing runtime dependencies.
if ([StringComparer]::OrdinalIgnoreCase.Equals(
    $Destination.TrimEnd('\', '/'), $SharedRuntimeDirectory.TrimEnd('\', '/'))) {
    return
}

$RuntimeProjects = @(
    "iCAX-Engine\foundation\Data",
    "iCAX-Engine\foundation\Task",
    "iCAX-Engine\foundation\GeometryData",
    "iCAX-Engine\foundation\GeometryAdapter",
    "iCAX-Engine\foundation\GeometryAlgo",
    "iCAX-Engine\framework\ApplicationContext",
    "iCAX-Engine\framework\Behaviour",
    "iCAX-Engine\framework\SDO",
    "iCAX-Engine\framework\Database",
    "iCAX-Engine\framework\PDO",
    "iCAX-Engine\framework\ProductContext",
    "iCAX-Engine\framework\ProjectContext",
    "iCAX-Engine\framework\Project",
    "iCAX-Engine\framework\Resources",
    "iCAX-Engine\framework\Services",
    "iCAX-Engine\framework\EntityViewRuntime",
    "iCAX-Plugins\geometry\ExtrusionRecognition",
    "iCAX-Plugins\cad\OpenCascadeResourceImport",
    "iCAX-Plugins\cam\IntentToolpath",
    "iCAX-Plugins\cam\TubeIntentAdapter",
    # Machine owns the machine-component meta registrations used by CamRuntime.
    # Keep its DLL in lockstep with the test binary to avoid cross-module layout skew.
    "iCAX-Plugins\cam\Machine",
    "iCAX-Plugins\cam\CamRuntime",
    "iCAX-Plugins\common\Transform",
    "iCAX-Plugins\render\RenderData",
    "iCAX-Plugins\render\RenderInteraction"
)

foreach ($Project in $RuntimeProjects) {
    $Name = Split-Path -Path $Project -Leaf
    $SharedDll = Join-Path $Root ("{0}\{1}\{2}.dll" -f $Platform, $Configuration, $Name)
    $ProjectDll = Join-Path $Root ("{0}\{1}\{2}\{3}\{4}.dll" -f $Project, $Platform, $Configuration, $Name, $Name)
    $TargetDll = Join-Path $Destination ("{0}.dll" -f $Name)
    $Dll = if (Test-Path -LiteralPath $TargetDll) {
        $TargetDll
    } elseif (Test-Path -LiteralPath $SharedDll) {
        $SharedDll
    } elseif (Test-Path -LiteralPath $ProjectDll) {
        $ProjectDll
    } else {
        throw "CamRuntime test runtime dependency is missing: $SharedDll"
    }

    $SourceDll = Get-Item -LiteralPath $Dll
    if ([StringComparer]::OrdinalIgnoreCase.Equals($SourceDll.FullName, $TargetDll)) {
        continue
    }

    $TargetItem = Get-Item -LiteralPath $TargetDll -ErrorAction SilentlyContinue
    if ($null -eq $TargetItem -or
        $TargetItem.Length -ne $SourceDll.Length -or
        $TargetItem.LastWriteTimeUtc -lt $SourceDll.LastWriteTimeUtc) {
        Copy-Item -LiteralPath $SourceDll.FullName -Destination $Destination -Force
    }
}

# OpenCascade import and the Tube CSG converter load OCC through DLL imports.
# Keep the test directory self-contained so an older DLL beside the test binary
# cannot shadow the freshly built shared runtime.
Get-ChildItem -LiteralPath $SharedRuntimeDirectory -Filter "TK*.dll" | ForEach-Object {
    $TargetDll = Join-Path $Destination $_.Name
    if ([StringComparer]::OrdinalIgnoreCase.Equals($_.FullName, $TargetDll)) {
        return
    }
    $TargetItem = Get-Item -LiteralPath $TargetDll -ErrorAction SilentlyContinue
    if ($null -eq $TargetItem -or
        $TargetItem.Length -ne $_.Length -or
        $TargetItem.LastWriteTimeUtc -lt $_.LastWriteTimeUtc) {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
    }
}
