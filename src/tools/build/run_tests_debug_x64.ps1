param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$SolutionDir = (Resolve-Path (Join-Path $RepoRoot "src\icax")).Path + "\"

$MSBuildCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe"
)

$MSBuild = $MSBuildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $MSBuild) {
    throw "MSBuild.exe was not found in known Visual Studio locations."
}

$BuildProjects = @(
    "src\icax\foundation\Data\Data.vcxproj",
    "src\tests\icax\foundation\Data\DataTest\DataTest.vcxproj",
    "src\icax\foundation\ProcedureCall\ProcedureCall.vcxproj",
    "src\tests\icax\foundation\ProcedureCall\ProcedureCallTest\ProcedureCallTest.vcxproj",
    "src\icax\foundation\Mailbox\Mailbox.vcxproj",
    "src\tests\icax\foundation\Mailbox\MailboxTest\MailboxTest.vcxproj",
    "src\icax\foundation\PDO\PDO.vcxproj",
    "src\tests\icax\foundation\PDO\PDOTest\PDOTest.vcxproj",
    "src\icax\foundation\Task\Task.vcxproj",
    "src\tests\icax\foundation\Task\TaskTest\TaskTest.vcxproj"
)

$TestExecutables = @(
    "src\icax\$Platform\$Configuration\DataTest.exe",
    "src\icax\$Platform\$Configuration\ProcedureCallTest.exe",
    "src\icax\$Platform\$Configuration\MailboxTest.exe",
    "src\icax\$Platform\$Configuration\PDOTest.exe",
    "src\icax\$Platform\$Configuration\TaskTest.exe"
)

$ParentEnvironment = [Environment]::GetEnvironmentVariables()
$PathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
if ([string]::IsNullOrEmpty($PathValue)) {
    $PathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
}

function Invoke-CleanMSBuild {
    param([string]$Project)

    $ProjectPath = Join-Path $RepoRoot $Project
    $Process = [Diagnostics.ProcessStartInfo]::new($MSBuild)
    $Process.UseShellExecute = $false
    $Process.RedirectStandardOutput = $true
    $Process.RedirectStandardError = $true
    $Process.WorkingDirectory = $RepoRoot.Path

    $Arguments = @(
        $ProjectPath,
        "/t:Build",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:SolutionDir=$SolutionDir",
        "/p:BuildProjectReferences=false",
        "/v:minimal",
        "/nologo"
    )
    foreach ($Argument in $Arguments) {
        [void]$Process.ArgumentList.Add($Argument)
    }

    $Process.Environment.Clear()
    $Seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($Key in $ParentEnvironment.Keys) {
        $Name = [string]$Key
        if ($Name -ieq "Path") {
            continue
        }
        if ($Seen.Add($Name)) {
            $Process.Environment[$Name] = [string]$ParentEnvironment[$Key]
        }
    }
    $Process.Environment["Path"] = $PathValue

    $Running = [Diagnostics.Process]::Start($Process)
    $Output = $Running.StandardOutput.ReadToEnd() + $Running.StandardError.ReadToEnd()
    $Running.WaitForExit()

    return [pscustomobject]@{
        ExitCode = $Running.ExitCode
        Output = $Output
    }
}

foreach ($Project in $BuildProjects) {
    $Name = [IO.Path]::GetFileNameWithoutExtension($Project)
    Write-Output "BUILD $Name"
    $Result = Invoke-CleanMSBuild $Project
    $Messages = ($Result.Output -split "`r?`n") | Where-Object {
        $_ -match "(: warning | warning C\d+|: error | error C\d+|LNK\d+|MSB\d+)"
    }

    if ($Result.ExitCode -ne 0) {
        Write-Output "FAILED $Name"
        if ($Messages) {
            $Messages | Select-Object -First 180
        }
        else {
            ($Result.Output -split "`r?`n") | Select-Object -Last 180
        }
        exit $Result.ExitCode
    }

    if ($Messages) {
        Write-Output "OK_WITH_MESSAGES $Name"
        $Messages | Select-Object -First 80
    }
    else {
        Write-Output "OK $Name"
    }
}

$env:PATH = (Join-Path $RepoRoot "src\icax\$Platform\$Configuration") + ";" + $env:PATH

foreach ($TestExecutable in $TestExecutables) {
    $ExecutablePath = Join-Path $RepoRoot $TestExecutable
    Write-Output "TEST $([IO.Path]::GetFileNameWithoutExtension($ExecutablePath))"
    & $ExecutablePath
    if ($LASTEXITCODE -ne 0) {
        Write-Output "FAILED $([IO.Path]::GetFileNameWithoutExtension($ExecutablePath))"
        exit $LASTEXITCODE
    }
    Write-Output "OK $([IO.Path]::GetFileNameWithoutExtension($ExecutablePath))"
}
