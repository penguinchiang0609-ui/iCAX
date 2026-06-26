param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$SolutionDir = (Resolve-Path (Join-Path $RepoRoot "src\icax-engine")).Path + "\"

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
    "src\icax-engine\foundation\Data\Data.vcxproj",
    "src\tests\icax-engine\foundation\Data\DataTest\DataTest.vcxproj",
    "src\icax-engine\framework\Resources\Resources.vcxproj",
    "src\tests\icax-engine\framework\Resources\ResourcesTest\ResourcesTest.vcxproj",
    "src\icax-engine\framework\ApplicationContext\ApplicationContext.vcxproj",
    "src\tests\icax-engine\framework\ApplicationContext\ApplicationContextTest\ApplicationContextTest.vcxproj",
    "src\icax-engine\framework\CommandHandler\CommandHandler.vcxproj",
    "src\tests\icax-engine\framework\CommandHandler\CommandHandlerTest\CommandHandlerTest.vcxproj",
    "src\icax-engine\framework\Database\Database.vcxproj",
    "src\tests\icax-engine\framework\Database\DatabaseTest\DatabaseTest.vcxproj",
    "src\icax-engine\framework\Mailbox\Mailbox.vcxproj",
    "src\tests\icax-engine\framework\Mailbox\MailboxTest\MailboxTest.vcxproj",
    "src\icax-engine\framework\PDO\PDO.vcxproj",
    "src\tests\icax-engine\framework\PDO\PDOTest\PDOTest.vcxproj",
    "src\icax-engine\framework\ProductContext\ProductContext.vcxproj",
    "src\icax-engine\framework\ProjectContext\ProjectContext.vcxproj",
    "src\icax-engine\framework\Services\Services.vcxproj",
    "src\icax-engine\framework\Behaviour\Behaviour.vcxproj",
    "src\icax-engine\framework\Project\Project.vcxproj",
    "src\tests\icax-engine\framework\Project\ProjectTest\ProjectTest.vcxproj",
    "src\icax-engine\framework\Product\Product.vcxproj",
    "src\tests\icax-engine\framework\Product\ProductTest\ProductTest.vcxproj",
    "src\icax-engine\framework\ApplicationHost\ApplicationHost.vcxproj",
    "src\tests\icax-engine\framework\ApplicationHost\ApplicationHostTest\ApplicationHostTest.vcxproj",
    "src\icax-engine\foundation\Task\Task.vcxproj",
    "src\tests\icax-engine\foundation\Task\TaskTest\TaskTest.vcxproj"
)

$TestExecutables = @(
    "src\icax-engine\$Platform\$Configuration\DataTest.exe",
    "src\icax-engine\$Platform\$Configuration\ResourcesTest.exe",
    "src\icax-engine\$Platform\$Configuration\ApplicationContextTest.exe",
    "src\icax-engine\$Platform\$Configuration\CommandHandlerTest.exe",
    "src\icax-engine\$Platform\$Configuration\DatabaseTest.exe",
    "src\icax-engine\$Platform\$Configuration\ProjectTest.exe",
    "src\icax-engine\$Platform\$Configuration\ProductTest.exe",
    "src\icax-engine\$Platform\$Configuration\ApplicationHostTest.exe",
    "src\icax-engine\$Platform\$Configuration\MailboxTest.exe",
    "src\icax-engine\$Platform\$Configuration\PDOTest.exe",
    "src\icax-engine\$Platform\$Configuration\TaskTest.exe"
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

$env:PATH = (Join-Path $RepoRoot "src\icax-engine\$Platform\$Configuration") + ";" + $env:PATH

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
