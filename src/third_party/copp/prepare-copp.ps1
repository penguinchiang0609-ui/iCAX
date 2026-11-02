[CmdletBinding()]
param([string]$OutputDirectory='C:\icax-machine-kinematics\copp\0.2.3')
$ErrorActionPreference='Stop'
$target=[IO.Path]::GetFullPath($OutputDirectory)
if([IO.Path]::GetPathRoot($target) -ne 'C:\'){throw 'COPP dependencies must be prepared in an independent C: directory.'}
$name='copp-c-v0.2.3-windows-x86_64-msvc-dynamic'
$sha='904fe2cd034b786a3e282c364677f9ad5533157afa2432ea97638c5e05a5322a'
$receipt=Join-Path $target 'icax-copp.json'
function Copy-Notices {
 foreach($item in @('THIRD-PARTY-NOTICES.txt','third-party-manifest.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $item) -Destination $target -Force}
}
if(Test-Path -LiteralPath $receipt){
 $old=Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json
 if($old.archiveSha256 -eq $sha -and (Test-Path -LiteralPath (Join-Path $target 'bin/copp.dll'))){Copy-Notices;Write-Output $target;return}
 throw 'The existing COPP dependency directory does not match the pinned SDK.'
}
New-Item -ItemType Directory -Path $target -Force|Out-Null
$archive=Join-Path $target ($name+'.zip')
Invoke-WebRequest -Uri "https://github.com/TOPP-THU/copp/releases/download/v0.2.3/$name.zip" -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha){throw 'COPP SDK checksum mismatch.'}
$extract=Join-Path $target 'archive'
Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
$source=Join-Path $extract $name
foreach($item in @('include','lib','bin','LICENSE')){Copy-Item -LiteralPath (Join-Path $source $item) -Destination $target -Recurse -Force}
# C ABI ownership keeps the Release Rust SDK separate from iCAX's Debug CRT.
@{version='0.2.3';commit='78bc8fe907cb425be7c29b74ae3b1c44a79aaae9';archiveSha256=$sha;interface='C ABI';license='MIT';source='https://github.com/TOPP-THU/copp/releases/tag/v0.2.3'}|ConvertTo-Json|Set-Content -LiteralPath $receipt -Encoding utf8
Copy-Notices
Write-Output $target
