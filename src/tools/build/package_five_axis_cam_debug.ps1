param([string]$OutputDirectory = 'output/releases/FiveAxisCAM-Debug-20261017', [string]$NativeDirectory = 'src/x64/Debug')
$ErrorActionPreference='Stop'
function Copy-PackageSourceTree([string]$Source,[string]$Destination){
 New-Item -ItemType Directory -Path $Destination -Force | Out-Null
 foreach($entry in Get-ChildItem -LiteralPath $Source){
  if($entry.PSIsContainer){
   if($entry.Name -eq '__pycache__'){continue}
   Copy-PackageSourceTree $entry.FullName (Join-Path $Destination $entry.Name)
  }elseif($entry.Extension -ne '.pyc'){
   Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $Destination $entry.Name)
  }
 }
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$build=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($NativeDirectory)){$NativeDirectory}else{Join-Path $repo $NativeDirectory}))
$out=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $repo $OutputDirectory}))
if(Test-Path $out){throw "Output already exists: $out"}
$dump='C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe'
$mt='C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe'
$debugCrtRoots=@(Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Redist/MSVC' -Directory|Sort-Object Name -Descending|ForEach-Object{Join-Path $_.FullName 'debug_nonredist/x64/Microsoft.VC143.DebugCRT'}|Where-Object{Test-Path -LiteralPath $_})
$externalDependencies=@{}
New-Item -ItemType Directory -Path $out | Out-Null
$manifestText=[IO.File]::ReadAllText((Join-Path $repo 'src/apps/five-axis-cam/product.manifest.json')).Replace('../../${Platform}/${Configuration}/','../../')
$manifest=$manifestText|ConvertFrom-Json
$queue=[Collections.Generic.Queue[string]]::new()
$queue.Enqueue('FiveAxisCAM.exe');$queue.Enqueue('CefUIContainer.dll');$queue.Enqueue('libEGL.dll');$queue.Enqueue('libGLESv2.dll');$queue.Enqueue('vk_swiftshader.dll');$queue.Enqueue('vulkan-1.dll')
foreach($prop in $manifest.backend.modules.PSObject.Properties){foreach($module in $prop.Value){$queue.Enqueue([IO.Path]::GetFileName($module))}}
foreach($handler in $manifest.backend.resources.handlers){$queue.Enqueue([IO.Path]::GetFileName($handler.module))}
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($queue.Count){
 $name=$queue.Dequeue();if(!$seen.Add($name)){continue}
 $source=if($externalDependencies.ContainsKey($name)){$externalDependencies[$name]}else{Join-Path $build $name}
 if(!(Test-Path $source)){throw "Missing runtime: $name"}
 Copy-Item -LiteralPath $source -Destination $out
 $deps=& $dump /nologo /dependents $source
 if($LASTEXITCODE){throw "Dependency scan failed: $name"}
 foreach($line in $deps){if($line -match '^\s+([\w.\-]+\.dll)\s*$'){
  $dep=$Matches[1]
  if(Test-Path (Join-Path $build $dep)){$queue.Enqueue($dep)}
  elseif($dep -match '^(?i:msvcp|msvcr|vcruntime|concrt)[\w.]*\.dll$|^(?i:ucrtbased)\.dll$'){
   $candidates=@($debugCrtRoots|ForEach-Object{Join-Path $_ $dep})+@(Join-Path "$env:windir/System32" $dep)
   $runtimeSource=$candidates|Where-Object{Test-Path -LiteralPath $_ -PathType Leaf}|Select-Object -First 1
   if(!$runtimeSource){throw "Missing local C/C++ runtime $dep from $name"}
   $externalDependencies[$dep]=$runtimeSource;$queue.Enqueue($dep)
  }
  elseif($dep -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path (Join-Path "$env:windir/System32" $dep))){throw "Unresolved dependency $dep from $name"}
 }}
}
foreach($name in @('chrome_100_percent.pak','chrome_200_percent.pak','icudtl.dat','resources.pak','v8_context_snapshot.bin','vk_swiftshader_icd.json')){Copy-Item (Join-Path $build $name) $out}
Copy-Item (Join-Path $build 'locales') $out -Recurse
New-Item -ItemType Directory -Path (Join-Path $out 'Licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'Licenses/Coal') -Destination (Join-Path $out 'Licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $build 'Licenses/OrocosKDL') -Destination (Join-Path $out 'Licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $build 'Licenses/FixtureGeometry') -Destination (Join-Path $out 'Licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $build 'Licenses/MachineKinematicsServices') -Destination (Join-Path $out 'Licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $build 'Licenses/DescartesLight') -Destination (Join-Path $out 'Licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $build 'Licenses/COPP') -Destination (Join-Path $out 'Licenses') -Recurse
if(Test-Path -LiteralPath (Join-Path $build 'Licenses/OMPL')){Copy-Item -LiteralPath (Join-Path $build 'Licenses/OMPL') -Destination (Join-Path $out 'Licenses') -Recurse}
foreach($relative in @('apps/five-axis-cam/webpage','apps/five-axis-cam/protocol','apps/five-axis-cam/resources','apps/_shared/workbench','iCAX-UI/SDK','iCAX-UI/AppProxy','iCAX-UI/ProductProxy','iCAX-UI/ProjectProxy','iCAX-UI/SceneProxy','iCAX-UI/UI')){
 $dest=Join-Path $out $relative
 New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null
 Copy-PackageSourceTree (Join-Path $repo "src/$relative") $dest
}
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $out 'apps/five-axis-cam/product.manifest.json'),$manifestText,$utf8)
[IO.File]::WriteAllText((Join-Path $out 'Application.Product.Setting'),"apps/five-axis-cam/product.manifest.json`n",$utf8)
$branding=[IO.File]::ReadAllLines((Join-Path $repo 'src/apps/Branding.Setting'))|Where-Object{ $_.StartsWith('platformIcon=') -or $_.StartsWith('product.icax.five-axis-cam=') }
foreach($line in $branding){$rel='apps/'+$line.Substring($line.IndexOf('=')+1).Trim();$dest=Join-Path $out $rel;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null;Copy-Item (Join-Path $repo "src/$rel") $dest}
[IO.File]::WriteAllLines((Join-Path $out 'apps/Branding.Setting'),$branding,$utf8)
New-Item -ItemType Directory -Path (Join-Path $out 'Setting')|Out-Null
[IO.File]::WriteAllText((Join-Path $out 'Setting/UIContainer.Setting'),"type=cef`nmodulePath=CefUIContainer.dll`nsdoPollIntervalMS=16`n",$utf8)
[IO.File]::WriteAllText((Join-Path $out 'Start-FiveAxisCAM.cmd'),"@echo off`r`nset `"ICAX_USER_DATA_ROOT=%~dp0UserData`"`r`ncd /d `"%~dp0`"`r`nstart `"`" `"%~dp0FiveAxisCAM.exe`"`r`n",[Text.Encoding]::ASCII)
$tempManifest=Join-Path $env:TEMP ('icax-package-'+[guid]::NewGuid()+'.manifest')
& $mt "-inputresource:$out/FiveAxisCAM.exe;#1" "-out:$tempManifest" | Out-Null
if($LASTEXITCODE){throw 'Cannot extract executable manifest'}
$text=[IO.File]::ReadAllText($tempManifest).Replace('requireAdministrator','asInvoker')
[IO.File]::WriteAllText($tempManifest,$text,$utf8)
& $mt -manifest $tempManifest "-outputresource:$out/FiveAxisCAM.exe;#1" | Out-Null
if($LASTEXITCODE){throw 'Cannot update executable manifest'}
& $mt "-inputresource:$out/FiveAxisCAM.exe;#1" "-out:$tempManifest" | Out-Null
if([IO.File]::ReadAllText($tempManifest) -notmatch 'level="asInvoker"'){throw 'Execution level validation failed'}
$debugDeps=& $dump /nologo /dependents (Join-Path $out 'FiveAxisCAM.dll')
if(($debugDeps -join "`n") -notmatch '(?i)(msvcp140d|vcruntime140d|ucrtbased)'){throw 'FiveAxisCAM.dll is not Debug'}
foreach($section in $manifest.backend.modules.PSObject.Properties){foreach($module in $section.Value){if($module.Contains('${') -or !(Test-Path -LiteralPath (Join-Path "$out/apps/five-axis-cam" $module))){throw "Unresolved packaged module: $module"}}}
foreach($handler in $manifest.backend.resources.handlers){if(!(Test-Path -LiteralPath (Join-Path "$out/apps/five-axis-cam" $handler.module))){throw "Unresolved packaged importer: $($handler.module)"}}
$forbidden=@(Get-ChildItem -LiteralPath $out -Recurse -File|Where-Object{$_.FullName -match '[\\/](UserData|Cache|Logs|node_modules|__pycache__)[\\/]' -or $_.Extension -in @('.tdlic','.pem','.key','.pfx','.p12','.log','.i5cam','.pdb','.pyc')})
if($forbidden.Count){throw "Unexpected private or development files in package: $($forbidden.FullName -join ', ')"}
$inventory=@(Get-ChildItem $out -Recurse -File|ForEach-Object{[pscustomobject]@{path=$_.FullName.Substring($out.Length+1);bytes=$_.Length;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}})
$inventory|ConvertTo-Json -Depth 4|Set-Content (Join-Path $out 'package-files.json') -Encoding utf8
Write-Output "Packaged $($inventory.Count) files: $out"
