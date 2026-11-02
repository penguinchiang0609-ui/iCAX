param(
    [string]$NativeDirectory = 'src/x64/Debug',
    [string]$OutputDirectory = 'output/run/five-axis-cam-current',
    [string]$LauncherPath = '',
    [switch]$PrepareOnly,
    [switch]$UseSourceWebpage
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$nativeRoot = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($NativeDirectory)) { $NativeDirectory } else { Join-Path $repository $NativeDirectory }))
$outputRoot = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repository $OutputDirectory }))
if ($nativeRoot -eq $outputRoot) { throw 'The development run directory must differ from the build directory.' }
$program = Join-Path $nativeRoot 'FiveAxisCAM.exe'
if ($LauncherPath) { $program = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($LauncherPath)) { $LauncherPath } else { Join-Path $repository $LauncherPath })) }
elseif (!(Test-Path -LiteralPath $program -PathType Leaf)) {
    # The desktop launcher is built separately; native modules always come from NativeDirectory.
    $program = Join-Path $repository 'output/tests/five-axis-runtime-isolated/FiveAxisCAM.exe'
}
if (!(Test-Path -LiteralPath $program -PathType Leaf)) { throw "Build the FiveAxisCAM desktop launcher first: $program" }
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
# Only program/runtime files are staged. Never copy user settings, projects or test data.
Get-ChildItem -LiteralPath $nativeRoot -File -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $outputRoot $_.Name) -Force
}
Copy-Item -LiteralPath $program -Destination (Join-Path $outputRoot 'FiveAxisCAM.exe') -Force
# The development launcher uses the caller's token. Do not prompt for elevation
# when opening a normal CAD/CAM project or running an isolated acceptance check.
$manifestTool = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe'
if (!(Test-Path -LiteralPath $manifestTool -PathType Leaf)) { throw 'Windows SDK manifest tool is required to prepare the development launcher.' }
$launcherManifest = Join-Path $outputRoot 'FiveAxisCAM.launch.manifest'
& $manifestTool "-inputresource:$outputRoot/FiveAxisCAM.exe;#1" "-out:$launcherManifest" | Out-Null
if ($LASTEXITCODE) { throw 'Cannot read the development launcher manifest.' }
$launcherManifestText = [IO.File]::ReadAllText($launcherManifest).Replace('requireAdministrator','asInvoker')
[IO.File]::WriteAllText($launcherManifest,$launcherManifestText,[Text.UTF8Encoding]::new($false))
& $manifestTool -manifest $launcherManifest "-outputresource:$outputRoot/FiveAxisCAM.exe;#1" | Out-Null
if ($LASTEXITCODE) { throw 'Cannot set the development launcher execution level.' }
& $manifestTool "-inputresource:$outputRoot/FiveAxisCAM.exe;#1" "-out:$launcherManifest" | Out-Null
if ($LASTEXITCODE -or [IO.File]::ReadAllText($launcherManifest) -notmatch 'level="asInvoker"') { throw 'Development launcher must run without elevation.' }
$coalNotices = Join-Path $nativeRoot 'Licenses/Coal'
if (!(Test-Path -LiteralPath $coalNotices)) { throw 'Missing Coal third-party notices. Build CoalCollisionServices first.' }
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'Licenses')) | Out-Null
Copy-Item -LiteralPath $coalNotices -Destination (Join-Path $outputRoot 'Licenses') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $nativeRoot 'Licenses/OrocosKDL') -Destination (Join-Path $outputRoot 'Licenses') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $nativeRoot 'Licenses/COPP') -Destination (Join-Path $outputRoot 'Licenses') -Recurse -Force
$cefRoot = Join-Path $repository 'src/x64/Debug'
foreach ($name in @('chrome_100_percent.pak','chrome_200_percent.pak','icudtl.dat','resources.pak','v8_context_snapshot.bin','vk_swiftshader_icd.json')) {
    Copy-Item -LiteralPath (Join-Path $cefRoot $name) -Destination (Join-Path $outputRoot $name) -Force
}
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'locales')) | Out-Null
Get-ChildItem -LiteralPath (Join-Path $cefRoot 'locales') -File -Filter '*.pak' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $outputRoot 'locales') -Force
}
$productRoot = Join-Path $outputRoot 'apps/five-axis-cam'
[IO.Directory]::CreateDirectory($productRoot) | Out-Null
Copy-Item -LiteralPath (Join-Path $repository 'src/apps/five-axis-cam/resources') -Destination $productRoot -Recurse -Force
$manifestText = [IO.File]::ReadAllText((Join-Path $repository 'src/apps/five-axis-cam/product.manifest.json')).Replace('../../${Platform}/${Configuration}/','../../')
$manifest = $manifestText | ConvertFrom-Json
$modules = @($manifest.backend.modules.PSObject.Properties | ForEach-Object { $_.Value })
$modules += @($manifest.backend.resources.handlers | ForEach-Object { $_.module })
foreach ($module in ($modules | Sort-Object -Unique)) {
    if ($module.Contains('${') -or !(Test-Path -LiteralPath (Join-Path $productRoot $module) -PathType Leaf)) {
        throw "Missing or unresolved runtime module: $module"
    }
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $productRoot 'product.manifest.json'),$manifestText,$utf8)
[IO.File]::WriteAllText((Join-Path $outputRoot 'Application.Product.Setting'),"apps/five-axis-cam/product.manifest.json`n",$utf8)
# Stage the selected product and platform window icons with their configuration.
$brandingLines = [IO.File]::ReadAllLines((Join-Path $repository 'src/apps/Branding.Setting')) | Where-Object {
    $_.StartsWith('platformIcon=') -or $_.StartsWith("product.$($manifest.productId)=")
}
foreach ($line in $brandingLines) {
    $relativeIcon = $line.Substring($line.IndexOf('=') + 1).Trim()
    $destinationIcon = Join-Path $outputRoot "apps/$relativeIcon"
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationIcon)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repository "src/apps/$relativeIcon") -Destination $destinationIcon -Force
}
if ($brandingLines.Count -ne 2) { throw 'Platform and FiveAxisCAM icon declarations are required.' }
[IO.File]::WriteAllLines((Join-Path $outputRoot 'apps/Branding.Setting'),$brandingLines,$utf8)
# A prepared runnable program owns its frontend. Editing workspace files must
# never change the modules a user loads on the next launch.
$webAssetInventory = @()
if ($UseSourceWebpage) {
    $webRoot = (Join-Path $repository 'src/iCAX-UI/SDK/AppShell').Replace('\','/')
} else {
    foreach ($relativeTree in @('apps/five-axis-cam/webpage','apps/five-axis-cam/protocol','apps/_shared/workbench','iCAX-UI/SDK','iCAX-UI/AppProxy','iCAX-UI/ProductProxy','iCAX-UI/ProjectProxy','iCAX-UI/SceneProxy','iCAX-UI/UI')) {
        $assetSource = [IO.Path]::GetFullPath((Join-Path $repository "src/$relativeTree"))
        $assetTarget = [IO.Path]::GetFullPath((Join-Path $outputRoot $relativeTree))
        if ($assetTarget.Equals($assetSource,[StringComparison]::OrdinalIgnoreCase) -or !$assetTarget.StartsWith($outputRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid frontend staging path.' }
        foreach ($asset in Get-ChildItem -LiteralPath $assetSource -Recurse -File) {
            $assetRelative = $asset.FullName.Substring($assetSource.Length+1)
            $assetDestination = Join-Path $assetTarget $assetRelative
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($assetDestination)) | Out-Null
            $beforeHash = (Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash
            Copy-Item -LiteralPath $asset.FullName -Destination $assetDestination -Force
            if ((Get-FileHash -LiteralPath $assetDestination -Algorithm SHA256).Hash -ne $beforeHash -or (Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash -ne $beforeHash) { throw "Frontend changed during staging: $($asset.FullName)" }
            $webAssetInventory += [pscustomobject]@{ path="$relativeTree/$($assetRelative.Replace('\','/'))"; sha256=$beforeHash }
        }
    }
    # Detect a second edit to an already copied module before completing staging.
    foreach ($asset in $webAssetInventory) {
        if ((Get-FileHash -LiteralPath (Join-Path $repository "src/$($asset.path)") -Algorithm SHA256).Hash -ne $asset.sha256) { throw "Frontend changed during staging: $($asset.path)" }
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot 'frontend-files.json'),($webAssetInventory | ConvertTo-Json -Depth 3),$utf8)
    $webRoot = (Join-Path $outputRoot 'iCAX-UI/SDK/AppShell').Replace('\','/')
}
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'Setting')) | Out-Null
$browserRoot = (Join-Path $outputRoot 'UserData/Browser').Replace('\','/')
$browserCache = "$browserRoot/Cache"
$browserLogs = (Join-Path $outputRoot 'UserData/Logs').Replace('\','/')
foreach ($directory in @($browserRoot, $browserCache, $browserLogs)) { [IO.Directory]::CreateDirectory($directory) | Out-Null }
# Keep CEF's profile explicit in the development run directory. Shell elevation
# can discard ICAX_USER_DATA_ROOT; two launchers must not then share its lock.
[IO.File]::WriteAllText((Join-Path $outputRoot 'Setting/UIContainer.Setting'),"type=cef`nmodulePath=CefUIContainer.dll`nwebPageRoot=$webRoot`nsdoPollIntervalMS=16`nuserDataPath=$browserRoot`ncachePath=$browserCache`nlogFile=$browserLogs/cef.log`n",$utf8)
Write-Output "Prepared native three-dimensional CAM: $outputRoot"
if (!$PrepareOnly) {
    $previousDataRoot = $env:ICAX_USER_DATA_ROOT
    try {
        $env:ICAX_USER_DATA_ROOT = Join-Path $outputRoot 'UserData'
        # The user explicitly requests this visible interactive application.
        $process = Start-Process -FilePath (Join-Path $outputRoot 'FiveAxisCAM.exe') -WorkingDirectory $outputRoot -WindowStyle Normal -PassThru
        Write-Output "Started FiveAxisCAM, process $($process.Id)"
    } finally { $env:ICAX_USER_DATA_ROOT = $previousDataRoot }
}
