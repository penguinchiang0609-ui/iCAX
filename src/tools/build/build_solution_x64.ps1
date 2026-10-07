#requires -Version 7.0
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateRange(1, 64)]
    [int]$Parallel = 4,

    [string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$solution = Join-Path $sourceRoot 'iCAX.slnx'
$outputDirectory = Join-Path $sourceRoot "x64/$Configuration"
$logDirectory = Join-Path $sourceRoot '.codex_tmp/build'
[IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$buildLog = Join-Path $logDirectory "$Configuration-x64.log"

if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $MSBuildPath = & $vswhere -latest -products '*' -version '[17.14,)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find 'MSBuild\Current\Bin\amd64\MSBuild.exe' | Select-Object -First 1
    }
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw 'Install Visual Studio 2022 17.14+ with Desktop development with C++, or supply -MSBuildPath.'
}

# Fail before touching any binaries if this configuration is currently running.
foreach ($file in @(Get-ChildItem -LiteralPath $outputDirectory -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.exe', '.dll' })) {
    try {
        $probe = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Write,
            ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        $probe.Dispose()
    } catch {
        throw "Build output is in use or not writable: $($file.FullName). Save and close that iCAX/test instance before building. $($_.Exception.Message)"
    }
}

function Invoke-BuildProcess([string]$Executable, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $sourceRoot
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    # Some hosts expose both Path and PATH; MSBuild's child tasks reject that.
    $start.Environment.Clear()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ($entry.Key -ine 'Path' -and $seen.Add([string]$entry.Key)) {
            $start.Environment[[string]$entry.Key] = [string]$entry.Value
        }
    }
    $start.Environment['Path'] = [Environment]::GetEnvironmentVariable('Path')
    $start.Environment['MSBUILDDISABLENODEREUSE'] = '1'
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "$Executable failed with exit code $($process.ExitCode). Build log: $buildLog"
        }
    } finally { $process.Dispose() }
}

$occtLibDirectory = if ($Configuration -eq 'Debug') { 'libd' } else { 'lib' }
$occtRoot = Join-Path $sourceRoot 'third_party/opencascade/install/vs2022-x64'
$occtProps = [IO.File]::ReadAllText((Join-Path $sourceRoot 'third_party/opencascade/OpenCascade.props'))
$occtLibraries = [regex]::Matches($occtProps, '\bTK\w*\.lib\b').Value | Sort-Object -Unique
$missingOcctLibraries = @($occtLibraries | Where-Object {
    !(Test-Path -LiteralPath (Join-Path $occtRoot "win64/vc14/$occtLibDirectory/$_") -PathType Leaf)
})
if ($missingOcctLibraries.Count -or !(Test-Path -LiteralPath (Join-Path $occtRoot 'inc/Standard.hxx'))) {
    Write-Host "Preparing OpenCascade $Configuration x64 (first build can take several minutes)..."
    Invoke-BuildProcess (Join-Path $PSHOME 'pwsh.exe') @(
        '-NoProfile', '-File', (Join-Path $PSScriptRoot 'build-opencascade.ps1'),
        '-Configuration', $Configuration, '-Parallel', [string]$Parallel
    )
}

Write-Host "Building $solution [$Configuration|x64]"
Invoke-BuildProcess $MSBuildPath @(
    $solution, '/t:Build', "/p:Configuration=$Configuration", '/p:Platform=x64',
    "/m:$Parallel", '/nr:false', '/nologo', '/v:minimal',
    "/flp:logfile=$buildLog;verbosity=normal;encoding=UTF-8"
)
Write-Host "Build completed: $outputDirectory"
Write-Host "Build log: $buildLog"
