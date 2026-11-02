param(
    [string]$BinaryDirectory='output/tests/coal-optimized-build',
    [string]$ProjectPath='output/tests/egg-480-generation/egg-480-cut-generated.i5cam',
    [string]$ReportDirectory='output/tests/ompl-coal-evaluation/repeat'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$binary=[IO.Path]::GetFullPath((Join-Path $root $BinaryDirectory))
$project=[IO.Path]::GetFullPath((Join-Path $root $ProjectPath))
$report=[IO.Path]::GetFullPath((Join-Path $root $ReportDirectory))
$test=Join-Path $binary 'FiveAxisCAMTest.exe'
foreach($path in @($test,$project,(Join-Path $binary 'OmplPlanningServices.dll'),(Join-Path $binary 'CoalCollisionServices.dll'))){if(!(Test-Path -LiteralPath $path)){throw "Missing evaluation input: $path"}}
New-Item -ItemType Directory -Path $report -Force | Out-Null
& $test '--gtest_filter=MotionPlanning.*:CoalDistance.*' "--gtest_output=xml:$(Join-Path $report 'service-tests.xml')" *> (Join-Path $report 'service-tests.log')
if($LASTEXITCODE){throw "Planning service regression failed; see $report"}
$oldProject=$env:ICAX_OMPL_PROJECT;$oldReport=$env:ICAX_OMPL_REPORT;$oldIndices=$env:ICAX_OMPL_INDICES
try {
    $env:ICAX_OMPL_PROJECT=$project
    $env:ICAX_OMPL_REPORT=Join-Path $report 'egg-479-report.json'
    Remove-Item Env:ICAX_OMPL_INDICES -ErrorAction SilentlyContinue
    & $test '--gtest_filter=MotionPlanningEvaluation.DISABLED_RealEggConnections' --gtest_also_run_disabled_tests *> (Join-Path $report 'native-evaluation.log')
    $result=$LASTEXITCODE
} finally {
    $env:ICAX_OMPL_PROJECT=$oldProject;$env:ICAX_OMPL_REPORT=$oldReport;$env:ICAX_OMPL_INDICES=$oldIndices
}
Get-Content -LiteralPath (Join-Path $report 'native-evaluation.log') -Tail 12
exit $result
