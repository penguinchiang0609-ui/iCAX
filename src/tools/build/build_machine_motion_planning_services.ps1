[CmdletBinding()]
param([string]$DependencyRoot='',[string]$FrameworkBinaryDirectory,[int]$Jobs=4,[switch]$SkipKinematicsBuild)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if(!$DependencyRoot){$DependencyRoot=Join-Path $repoRoot 'output/work/machine-kinematics'}
$taskRoot=[IO.Path]::GetFullPath($DependencyRoot)
if([IO.Path]::GetPathRoot($taskRoot) -eq 'C:\'){throw 'Use a motion build directory on D:; do not place iCAX build data on the system drive.'}
if(!$FrameworkBinaryDirectory){$FrameworkBinaryDirectory=Join-Path $repoRoot 'src/x64/Debug'}
if(!$SkipKinematicsBuild){& (Join-Path $PSScriptRoot 'build_machine_kinematics_services.ps1') -DependencyRoot $taskRoot -FrameworkBinaryDirectory $FrameworkBinaryDirectory -Jobs $Jobs}
$descartes=Join-Path $repoRoot 'src/third_party/descartes-light/install/x64/Debug/lib/icax_descartes_light.lib'
if(!(Test-Path -LiteralPath $descartes)){throw 'Build the existing pinned Descartes Light dependency before the motion service (prepare-descartes-light.ps1).'}
$coppRoot=Join-Path $taskRoot 'copp/0.2.3'
& (Join-Path $repoRoot 'src/third_party/copp/prepare-copp.ps1') -OutputDirectory $coppRoot
function Run([string]$Label,[string]$Executable,[string[]]$Arguments){$log=Join-Path $taskRoot ($Label+'.log');Write-Host $Label;& $Executable @Arguments *> $log;if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log -Tail 25;throw "$Label failed. See $log"}}
Run 'motion-config' 'cmake.exe' @('-S',"$repoRoot/src/iCAX-Plugins/geometry/MachineMotionPlanningServices",'-B',"$taskRoot/motion-build",'-G','Visual Studio 17 2022','-A','x64',"-DICAX_BINARY_DIRECTORY=$FrameworkBinaryDirectory","-DICAX_KINEMATICS_BUILD=$taskRoot/plugin-build/Debug","-DICAX_KINEMATICS_INSTALL=$taskRoot/install","-DICAX_COPP_ROOT=$coppRoot",'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL',"-DCMAKE_INSTALL_PREFIX=$taskRoot/install")
Run 'motion-build' 'cmake.exe' @('--build',"$taskRoot/motion-build",'--config','Debug',"-j$Jobs")
Run 'motion-tests' 'ctest.exe' @('--test-dir',"$taskRoot/motion-build",'-C','Debug','--output-on-failure')
Run 'motion-install' 'cmake.exe' @('--install',"$taskRoot/motion-build",'--config','Debug')
$license=Join-Path $taskRoot 'install/licenses/DescartesLight'
New-Item -ItemType Directory -Force -Path $license | Out-Null
Copy-Item -Path (Join-Path $repoRoot 'src/third_party/descartes-light/install/x64/Debug/licenses/*') -Destination $license -Force
Write-Host "Built and tested: $taskRoot/install/bin/MachineMotionPlanningServices.dll and updated DescartesPlanningServices.dll"
