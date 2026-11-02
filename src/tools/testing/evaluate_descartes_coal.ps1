param(
    [string]$BinaryDirectory='output/tests/coal-optimized-build',
    [string]$ReportDirectory='output/tests/descartes-coal-evaluation',
    [ValidateSet('disk','egg','both')][string]$Model='both'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$binary=[IO.Path]::GetFullPath((Join-Path $root $BinaryDirectory))
$report=[IO.Path]::GetFullPath((Join-Path $root $ReportDirectory))
$test=Join-Path $binary 'FiveAxisCAMTest.exe'
foreach($path in @($test,(Join-Path $binary 'DescartesPlanningServices.dll'),(Join-Path $binary 'CoalCollisionServices.dll'))){if(!(Test-Path -LiteralPath $path)){throw "Missing evaluation binary: $path"}}
New-Item -ItemType Directory -Path $report -Force | Out-Null
& $test '--gtest_filter=PathOrientation.*:CoalDistance.*:MotionPlanning.*:FiveAxisToolAxis.*:FiveAxisToolAxisDisplay.*:FiveAxisGeneration.*:FiveAxisOrder.*:FiveAxisRapidGeometry.*:FiveAxisRapid.*' "--gtest_output=xml:$(Join-Path $report 'regression.xml')" *> (Join-Path $report 'regression.log')
if($LASTEXITCODE){throw "Native regression failed; see $report"}
$savedProject=$env:ICAX_ORIENTATION_PROJECT;$savedReport=$env:ICAX_ORIENTATION_REPORT;$savedDisk=$env:ICAX_ORIENTATION_DISK_OUTPUT
$failed=$false
try {
    if($Model -in @('disk','both')){
        # Prepare a CURRENT project from the real STEP through native import,
        # scale, cut extraction, process preparation and exact path generation.
        # Historical project formats are not migrated by this evaluation.
        $env:ICAX_ORIENTATION_DISK_OUTPUT=Join-Path $report 'disk-current-generated.i5cam'
        & $test '--gtest_filter=FiveAxisDiskRegression.FiveTimesScaledDiskRetainsExteriorTrim' *> (Join-Path $report 'disk-input.log')
        if($LASTEXITCODE){throw "Real disk input generation failed; see $report"}
    }
    $inputs=@()
    if($Model -in @('disk','both')){$inputs+=@{name='disk';path=(Join-Path $report 'disk-current-generated.i5cam')}}
    if($Model -in @('egg','both')){$inputs+=@{name='egg';path=(Join-Path $root 'output/tests/egg-480-generation/egg-480-cut-generated.i5cam')}}
    foreach($input in $inputs){
        if(!(Test-Path -LiteralPath $input.path)){throw "Missing real model project: $($input.path)"}
        $before=(Get-FileHash -LiteralPath $input.path -Algorithm SHA256).Hash
        $env:ICAX_ORIENTATION_PROJECT=$input.path;$env:ICAX_ORIENTATION_REPORT=Join-Path $report "$($input.name)-report.json"
        & $test '--gtest_filter=PathOrientationEvaluation.DISABLED_RealSavedModel' --gtest_also_run_disabled_tests *> (Join-Path $report "$($input.name)-native.log")
        if($LASTEXITCODE){$failed=$true}
        $after=(Get-FileHash -LiteralPath $input.path -Algorithm SHA256).Hash
        if($before -ne $after){throw 'Evaluation changed the input project file'}
        @{model=$input.name;path=$input.path;sha256Before=$before;sha256After=$after;unchanged=($before -eq $after)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report "$($input.name)-input-hash.json") -Encoding utf8
        Get-Content -LiteralPath (Join-Path $report "$($input.name)-native.log") -Tail 10
    }
} finally {
    $env:ICAX_ORIENTATION_PROJECT=$savedProject;$env:ICAX_ORIENTATION_REPORT=$savedReport;$env:ICAX_ORIENTATION_DISK_OUTPUT=$savedDisk
}
if($failed){exit 1}
