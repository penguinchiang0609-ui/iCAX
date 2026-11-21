param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('Build','Rebuild','Clean','Test')][string]$Action = 'Build',
    [string]$Generator = 'Visual Studio 17 2022',
    [ValidateRange(1,32)][int]$Parallel = 1
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$moduleRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildRoot = Join-Path $moduleRoot ('build/' + $Configuration)
$installRoot = Join-Path $moduleRoot ('dist/' + $Configuration)

function Invoke-iPlaneNative([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
function Clear-iPlaneBuild {
    if (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')) {
        $cleanArguments = @('--build', $buildRoot, '--config', $Configuration, '--target', 'clean')
        if ($Generator.StartsWith('Visual Studio', [StringComparison]::OrdinalIgnoreCase)) {
            $cleanArguments += @('--', '/nodeReuse:false', '/v:minimal')
        }
        Invoke-iPlaneNative 'cmake' $cleanArguments
    }
    if (Test-Path -LiteralPath $installRoot) {
        # Clean only this project's generated installation for this configuration.
        $resolvedInstall = (Resolve-Path -LiteralPath $installRoot).Path
        $allowedInstallPrefix = [IO.Path]::GetFullPath((Join-Path $moduleRoot 'dist')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        if (!$resolvedInstall.StartsWith($allowedInstallPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to clean an installation outside this module'
        }
        Remove-Item -LiteralPath $resolvedInstall -Recurse -Force
    }
}

if ($Action -in @('Clean','Rebuild')) {
    Clear-iPlaneBuild
    if ($Action -eq 'Clean') { return }
}
$configureArguments = @('-S', $moduleRoot, '-B', $buildRoot, '-G', $Generator,
    '-DIPLANE_NESTING_BUILD_TESTS=ON', '-DIPLANE_NESTING_BUILD_EXAMPLES=ON')
if ($Generator.StartsWith('Visual Studio', [StringComparison]::OrdinalIgnoreCase)) {
    $configureArguments += @('-A', 'x64')
} else {
    $configureArguments += ('-DCMAKE_BUILD_TYPE=' + $Configuration)
}
Invoke-iPlaneNative 'cmake' $configureArguments
$buildArguments = @('--build', $buildRoot, '--config', $Configuration, '--parallel', [string]$Parallel)
if ($Action -ne 'Test') { $buildArguments += @('--target', 'iPlaneNesting') }
if ($Generator.StartsWith('Visual Studio', [StringComparison]::OrdinalIgnoreCase)) {
    $buildArguments += @('--', '/nodeReuse:false', '/v:minimal')
}
Invoke-iPlaneNative 'cmake' $buildArguments
if ($Action -eq 'Test') {
    Invoke-iPlaneNative 'ctest' @('--test-dir', $buildRoot, '-C', $Configuration,
        '--output-on-failure', '--test-output-size-passed', '65536', '--test-output-size-failed', '65536',
        '--output-junit', (Join-Path $buildRoot 'native-tests.xml'))
}
Invoke-iPlaneNative 'cmake' @('--install', $buildRoot, '--config', $Configuration,
    '--component', 'iPlaneNesting', '--prefix', $installRoot)
Write-Output ('iPlaneNesting: ' + (Join-Path $installRoot 'bin/iPlaneNesting.dll'))
