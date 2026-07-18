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

$Projects = @(
    "src\icax-engine\foundation\Data\Data.vcxproj",
    "src\icax-engine\foundation\Math\Math.vcxproj",
    "src\icax-engine\foundation\Geometry\Geometry.vcxproj",
    "src\icax-engine\foundation\Task\Task.vcxproj",
    "src\icax-engine\framework\Resources\Resources.vcxproj",
    "src\icax-engine\framework\Mailbox\Mailbox.vcxproj",
    "src\icax-engine\framework\PDO\PDO.vcxproj",
    "src\icax-engine\framework\ApplicationContext\ApplicationContext.vcxproj",
    "src\icax-engine\framework\ProductContext\ProductContext.vcxproj",
    "src\icax-engine\framework\ProjectContext\ProjectContext.vcxproj",
    "src\icax-engine\framework\Facades\Facades.vcxproj",
    "src\icax-engine\framework\Database\Database.vcxproj",
    "src\icax-engine\framework\Services\Services.vcxproj",
    "src\icax-engine\framework\Behaviour\Behaviour.vcxproj",
    "src\icax-engine\framework\Project\Project.vcxproj",
    "src\icax-engine\framework\Product\Product.vcxproj",
    "src\apps\sample\backend\component\SampleComponent\SampleComponent.vcxproj",
    "src\apps\sample\backend\service\SampleService\SampleService.vcxproj",
    "src\apps\sample\backend\behaviour\SampleBehaviour\SampleBehaviour.vcxproj",
    "src\icax-engine\framework\ApplicationHost\ApplicationHost.vcxproj",
    "src\iCAX-UI\WebPageHost\WebPageHost.vcxproj",
    "src\apps\cax-host\backend\CAXHost\CAXHost.vcxproj",
    "src\apps\mfc-sample\backend\MFCSample\MFCSample.vcxproj"
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

foreach ($Project in $Projects) {
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
