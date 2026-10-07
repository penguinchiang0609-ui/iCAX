param(
    [ValidateSet('baseline', 'fixed')][string]$Variant = 'baseline',
    [string]$ModuleDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$cefTestRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$cefTestRuntime = Join-Path $cefTestRepo 'src/x64/Debug'
if (!$ModuleDirectory) { $ModuleDirectory = $cefTestRuntime }
$ModuleDirectory = [IO.Path]::GetFullPath($ModuleDirectory)
if (!$OutputDirectory) { $OutputDirectory = Join-Path $cefTestRepo "output/tests/cef-shutdown/$Variant" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (!$OutputDirectory.StartsWith((Join-Path $cefTestRepo 'output/tests/cef-shutdown') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The isolated test output must be under output/tests/cef-shutdown'
}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$cefTestTools = 'C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64'
$cefTestQueue = [Collections.Generic.Queue[string]]::new()
$cefTestVisited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($cefTestName in @('CefUIContainer.dll', 'UIContainer.dll', 'Task.dll', 'libcef.dll', 'chrome_elf.dll', 'libEGL.dll', 'libGLESv2.dll', 'd3dcompiler_47.dll', 'vk_swiftshader.dll', 'vulkan-1.dll')) { $cefTestQueue.Enqueue($cefTestName) }
while ($cefTestQueue.Count) {
    $cefTestName = $cefTestQueue.Dequeue()
    if (!$cefTestVisited.Add($cefTestName)) { continue }
    $cefTestSource = Join-Path $ModuleDirectory $cefTestName
    if (!(Test-Path -LiteralPath $cefTestSource -PathType Leaf)) { $cefTestSource = Join-Path $cefTestRuntime $cefTestName }
    if (!(Test-Path -LiteralPath $cefTestSource -PathType Leaf)) { continue }
    Copy-Item -LiteralPath $cefTestSource -Destination (Join-Path $OutputDirectory $cefTestName)
    foreach ($cefTestDependency in (& (Join-Path $cefTestTools 'dumpbin.exe') /nologo /dependents $cefTestSource)) {
        if ($cefTestDependency -match '^\s+([\w.-]+\.dll)\s*$') { $cefTestQueue.Enqueue($Matches[1]) }
    }
}
foreach ($cefTestAsset in Get-ChildItem -LiteralPath $cefTestRuntime -File | Where-Object { $_.Extension -in @('.pak', '.dat', '.bin') }) {
    Copy-Item -LiteralPath $cefTestAsset.FullName -Destination (Join-Path $OutputDirectory $cefTestAsset.Name)
}
if (Test-Path -LiteralPath (Join-Path $cefTestRuntime 'locales')) { Copy-Item -LiteralPath (Join-Path $cefTestRuntime 'locales') -Destination $OutputDirectory -Recurse -Force }
$cefTestStart = [Diagnostics.ProcessStartInfo]::new('C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/amd64/MSBuild.exe')
$cefTestStart.UseShellExecute = $false
$cefTestStart.CreateNoWindow = $true
$cefTestStart.Environment.Clear()
$cefTestNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($cefTestEnvironment in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    if ($cefTestNames.Add([string]$cefTestEnvironment.Key)) { $cefTestStart.Environment[[string]$cefTestEnvironment.Key] = [string]$cefTestEnvironment.Value }
}
$cefTestFactory = if ($Variant -eq 'fixed') { '1' } else { '0' }
foreach ($cefTestArgument in @(
    (Join-Path $PSScriptRoot 'CefShutdown.native.vcxproj'), '/t:Build', '/p:Configuration=Debug', '/p:Platform=x64', '/p:BuildProjectReferences=false',
    "/p:ICAXCefHarnessOutput=$OutputDirectory", "/p:ICAXCefHarnessLibraries=$ModuleDirectory", "/p:ICAXCefHarnessFactory=$cefTestFactory",
    '/m:1', '/nr:false', '/nologo', '/v:minimal', "/flp:logfile=$OutputDirectory/build.log;verbosity=normal;encoding=UTF-8"
)) { [void]$cefTestStart.ArgumentList.Add($cefTestArgument) }
$cefTestProcess = [Diagnostics.Process]::Start($cefTestStart)
try {
    $cefTestProcess.WaitForExit()
    if ($cefTestProcess.ExitCode -ne 0) { throw "Harness build failed; see $OutputDirectory/build.log" }
} finally { $cefTestProcess.Dispose() }
$cefTestHashes = Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object { $_.Extension -in @('.exe', '.dll') } | ForEach-Object {
    [pscustomobject]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{ variant = $Variant; moduleSource = $ModuleDirectory; output = $OutputDirectory; files = @($cefTestHashes) } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'binary-manifest.json') -Encoding utf8
Write-Output "Built isolated $Variant CEF harness in $OutputDirectory"
