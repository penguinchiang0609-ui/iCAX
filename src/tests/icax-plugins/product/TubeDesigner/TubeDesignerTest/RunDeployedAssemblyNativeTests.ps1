param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,
    [string]$LogFile,
    [switch]$IncludePerformance
)

$ErrorActionPreference = 'Stop'
$RuntimeDirectory = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$TestExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$RepoDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..\..\..\..')).Path

# Fail before native resolution can fall back to the source tree. Validate the
# complete deployed package, including all template-owned Python contracts.
& (Join-Path $RepoDirectory 'src\tools\build\sync_tube_designer_templates.ps1') -OutputDirectory $RuntimeDirectory -ValidateOnly

$Filter = 'AssemblyTemplateSDOTest.*FinishedProduct*:AssemblyTemplateSDOTest.Applicability*:AssemblyTemplateSDOTest.EveryTemplateProvidesApplicableExampleProductWithoutManufacturing:AssemblyTemplateSDOTest.ExampleProductDimensionsRemainSeparateFromNativeManufacturing:AssemblyTemplateSDOTest.InsertAllowancesPreserveFinishedLengthsAndBuildNativeBlanks:AssemblyTemplateSDOTest.EveryTemplateExampleBuildsAllNativeManufacturingGeometry:AssemblyTemplateSDOTest.RoundFinishedProductsBuildNativeBlanksWithoutDerivedInputFields:AssemblyTemplateSDOTest.MiterFinishedProductNativeCutsMeetAtDeclaredGap:AssemblyTemplateSDOTest.FixedPlaneProcessesRejectRotatedProductAndSleeveRejectsUndeclaredLocks:AssemblyTemplateSDOTest.AssemblyBoundaryExamplesPassNativeContractsAndRejectOverlongCutStock:AssemblyTemplateSDOTest.InactiveRectRadiusDraftsKeepIntegratedStockAndNativeGeometry:AssemblyTemplateSDOTest.IndependentEqualCornerRadiiKeepFormedMeshesAndNativeBlanks:AssemblyTemplateSDOTest.ManufacturingOnlyRetainsCompleteRequestsAndSkipsFormedMeshes:AssemblyTemplateSDOTest.OptimizedManufacturingPreviewRejectsDisconnectedAndEmptyResults'
if ($IncludePerformance) {
    # Informational warmed medians and native phase timings, with fresh
    # resource identities on every sample; deliberately no speed threshold.
    $Filter += ':AssemblyTemplateSDOTest.OptimizedManufacturingPreviewPerformanceProbe'
}
if ($LogFile) {
    $LogFile = [IO.Path]::GetFullPath($LogFile)
    New-Item -ItemType Directory -Path (Split-Path -Parent $LogFile) -Force | Out-Null
}

# The acceptance application's InstallDirectory comes from current_path. Run
# beside the actual application data, even when the test exe is isolated.
Push-Location -LiteralPath $RuntimeDirectory
try {
    Write-Output "NATIVE Assembly runtime: $RuntimeDirectory"
    if ($LogFile) {
        & $TestExecutable "--gtest_filter=$Filter" 2>&1 | Tee-Object -FilePath $LogFile
    } else {
        & $TestExecutable "--gtest_filter=$Filter"
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Deployed assembly native tests failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
