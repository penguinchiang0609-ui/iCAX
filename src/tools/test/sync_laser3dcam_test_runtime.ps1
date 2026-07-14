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

$RuntimeProjects = @(
    "iCAX-Engine\foundation\Data",
    "iCAX-Engine\foundation\GeometryData",
    "iCAX-Engine\framework\ApplicationContext",
    "iCAX-Engine\framework\Behaviour",
    "iCAX-Engine\framework\CommandTargets",
    "iCAX-Engine\framework\Database",
    "iCAX-Engine\framework\Mailbox",
    "iCAX-Engine\framework\PDO",
    "iCAX-Engine\framework\ProductContext",
    "iCAX-Engine\framework\ProjectContext",
    "iCAX-Engine\framework\Resources",
    "iCAX-Engine\framework\Services",
    "iCAX-Plugins\cam\Laser3DCAM",
    "iCAX-Plugins\common\Transform",
    "iCAX-Plugins\input\InputPDO",
    "iCAX-Plugins\input\InputService",
    "iCAX-Plugins\render\CameraNavigation",
    "iCAX-Plugins\render\RenderData",
    "iCAX-Plugins\render\RenderInteraction",
    "iCAX-Plugins\render\RenderService"
)

foreach ($Project in $RuntimeProjects) {
    $Name = Split-Path -Path $Project -Leaf
    $SharedDll = Join-Path $Root ("{0}\{1}\{2}.dll" -f $Platform, $Configuration, $Name)
    $ProjectDll = Join-Path $Root ("{0}\{1}\{2}\{3}.dll" -f $Project, $Platform, $Configuration, $Name)
    $Dll = if (Test-Path -LiteralPath $ProjectDll) { $ProjectDll } else { $SharedDll }
    if (!(Test-Path -LiteralPath $Dll)) {
        throw "Laser3DCAM test runtime dependency is missing: $ProjectDll"
    }

    $SourceDll = Get-Item -LiteralPath $Dll
    $TargetDll = Join-Path $Destination ("{0}.dll" -f $Name)
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
