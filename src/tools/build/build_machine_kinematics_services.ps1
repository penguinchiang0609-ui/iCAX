[CmdletBinding()]
param(
    [string]$DependencyRoot='',
    [string]$FrameworkBinaryDirectory,
    [int]$Jobs=4,
    [switch]$SkipTests
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if(!$DependencyRoot){$DependencyRoot=Join-Path $repoRoot 'output/work/machine-kinematics'}
$taskRoot=[IO.Path]::GetFullPath($DependencyRoot)
if([IO.Path]::GetPathRoot($taskRoot) -eq 'C:\'){throw 'Use a dependency directory on D:; do not place iCAX build data on the system drive.'}
if(!$FrameworkBinaryDirectory){$FrameworkBinaryDirectory=Join-Path $repoRoot 'src/x64/Debug'}
$install=Join-Path $taskRoot 'install'
New-Item -ItemType Directory -Force -Path $taskRoot | Out-Null
function Run([string]$Label,[string]$Executable,[string[]]$Arguments){
    $log=Join-Path $taskRoot ($Label+'.log')
    Write-Host $Label
    & $Executable @Arguments *> $log
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log -Tail 35;throw "$Label failed ($LASTEXITCODE). Full log: $log"}
}
function Archive([string]$Name,[string]$Url,[string]$Sha,[string]$Directory,[string]$LocalArchive=''){
    $archive=Join-Path $taskRoot $Name
    if(!(Test-Path -LiteralPath $archive)){
        if($LocalArchive -and (Test-Path -LiteralPath $LocalArchive)){Copy-Item -LiteralPath $LocalArchive -Destination $archive}
        else {Invoke-WebRequest -Uri $Url -OutFile $archive}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Sha){throw "SHA256 mismatch: $archive"}
    if(!(Test-Path -LiteralPath (Join-Path $taskRoot $Directory))){Run ('extract-'+$Directory) 'tar.exe' @('-xzf',$archive,'-C',$taskRoot)}
}
Archive 'pinocchio-2.7.0.tar.gz' 'https://codeload.github.com/stack-of-tasks/pinocchio/tar.gz/refs/tags/v2.7.0' '413F66D573017041043F3963706E827470E9B9FAEECDF8E2D0B15974F241D104' 'pinocchio-2.7.0'
# The exact cmake gitlink recorded by the Pinocchio v2.7.0 source tag; never
# download jrl-cmakemodules master during a supposedly reproducible configure.
Archive 'jrl-cmake.tar.gz' 'https://codeload.github.com/jrl-umi3218/jrl-cmakemodules/tar.gz/fff972a050d2f8812c3c55bea92b6a67d71f615b' '3A4C3F3F2DBF82825E19228F749CCB4D7F9B0B9B47591C1CF012510535780AFE' 'jrl-cmakemodules-fff972a050d2f8812c3c55bea92b6a67d71f615b'
if(!(Test-Path -LiteralPath (Join-Path $taskRoot 'pinocchio-2.7.0/cmake/base.cmake'))){Copy-Item -Path (Join-Path $taskRoot 'jrl-cmakemodules-fff972a050d2f8812c3c55bea92b6a67d71f615b/*') -Destination (Join-Path $taskRoot 'pinocchio-2.7.0/cmake') -Recurse}
# This pinned jrl template leaves CMAKE_COMMAND unquoted. Re-install otherwise
# fails on Windows when CMake lives under Program Files. Fix only that exact
# build-script token; solver/library sources remain unmodified.
$uninstallTemplate=Join-Path $taskRoot 'pinocchio-2.7.0/cmake/cmake_uninstall.cmake.in'
$uninstallText=[IO.File]::ReadAllText($uninstallTemplate)
$uninstallPatched=$uninstallText.Replace('COMMAND @CMAKE_COMMAND@','COMMAND "@CMAKE_COMMAND@"')
if($uninstallPatched -ne $uninstallText){[IO.File]::WriteAllText($uninstallTemplate,$uninstallPatched,[Text.UTF8Encoding]::new($false))}
Archive 'ceres-2.1.0.tar.gz' 'https://codeload.github.com/ceres-solver/ceres-solver/tar.gz/refs/tags/2.1.0' 'CCBD716A93F65D4CB017E3090AE78809E02F5426DCE16D0EE2B4F8A4BA2411A8' 'ceres-solver-2.1.0'
Archive 'eigen-3.4.0.tar.gz' 'https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz' '8586084F71F9BDE545EE7FA6D00288B264A2B7AC3607B974E54D13E7162C1C72' 'eigen-3.4.0' (Join-Path $repoRoot 'src/third_party/coal/downloads/eigen-3.4.0.tar.gz')
Archive 'boost_1_87_0.tar.gz' 'https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz' 'F55C340AA49763B1925CCF02B2E83F35FDCF634C9D5164A2ACB87540173C741D' 'boost_1_87_0' (Join-Path $repoRoot 'src/third_party/coal/downloads/boost_1_87_0.tar.gz')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -version '[17.0,18.0)' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio 2022 x64 C++ tools are required.'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$boost=Join-Path $taskRoot 'boost_1_87_0'
if(!(Test-Path -LiteralPath (Join-Path $boost 'b2.exe'))){Push-Location $boost;try{Run 'boost-bootstrap' 'cmd.exe' @('/d','/c',"call `"$vcvars`" && bootstrap.bat --with-toolset=msvc")}finally{Pop-Location}}
if(!(Test-Path -LiteralPath (Join-Path $install 'lib/libboost_serialization-vc143-mt-gd-x64-1_87.lib'))){
    Push-Location $boost
    try {Run 'boost-build' 'cmd.exe' @('/d','/c',"call `"$vcvars`" && b2.exe -q -j$Jobs toolset=msvc-14.3 variant=debug runtime-link=shared link=static address-model=64 --with-filesystem --with-system --with-serialization --build-dir=`"$taskRoot\boost-build`" --prefix=`"$install`" install")}
    finally{Pop-Location}
}
$common=@('-G','Visual Studio 17 2022','-A','x64','-DCMAKE_POLICY_VERSION_MINIMUM=3.10',"-DCMAKE_INSTALL_PREFIX=$install","-DCMAKE_PREFIX_PATH=$install",'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL','-DCMAKE_CONFIGURATION_TYPES=Debug','-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /W3 /GR /EHsc /utf-8')
Run 'eigen-config' 'cmake.exe' (@('-S',"$taskRoot/eigen-3.4.0",'-B',"$taskRoot/eigen-c-build")+$common+@('-DBUILD_TESTING=OFF','-DEIGEN_BUILD_DOC=OFF','-DEIGEN_BUILD_PKGCONFIG=OFF'))
Run 'eigen-install' 'cmake.exe' @('--install',"$taskRoot/eigen-c-build",'--config','Debug')
Run 'ceres-config' 'cmake.exe' (@('-S',"$taskRoot/ceres-solver-2.1.0",'-B',"$taskRoot/ceres-build")+$common+@('-DMSVC_USE_STATIC_CRT=OFF','-DMINIGLOG=ON','-DGFLAGS=OFF','-DBUILD_TESTING=OFF','-DBUILD_EXAMPLES=OFF','-DBUILD_BENCHMARKS=OFF','-DBUILD_DOCUMENTATION=OFF','-DBUILD_SHARED_LIBS=OFF','-DSUITESPARSE=OFF','-DCXSPARSE=OFF','-DEIGENSPARSE=OFF','-DLAPACK=OFF','-DCUDA=OFF','-DSCHUR_SPECIALIZATIONS=OFF'))
Run 'ceres-build' 'cmake.exe' @('--build',"$taskRoot/ceres-build",'--config','Debug','--target','install',"-j$Jobs")
Run 'pinocchio-config' 'cmake.exe' (@('-S',"$taskRoot/pinocchio-2.7.0",'-B',"$taskRoot/pinocchio-build")+$common+@('-DBUILD_PYTHON_INTERFACE=OFF','-DBUILD_WITH_URDF_SUPPORT=OFF','-DBUILD_WITH_COLLISION_SUPPORT=OFF','-DBUILD_WITH_HPP_FCL_SUPPORT=OFF','-DBUILD_WITH_OPENMP_SUPPORT=OFF','-DBUILD_TESTING=OFF','-DBUILD_BENCHMARK=OFF','-DBUILD_UTILS=OFF','-DINSTALL_DOCUMENTATION=OFF','-DBUILD_DOCUMENTATION=OFF','-DBUILD_WITH_COMMIT_VERSION=OFF','-DBoost_USE_STATIC_LIBS=ON','-DBoost_USE_STATIC_RUNTIME=OFF','-DBoost_USE_DEBUG_LIBS=ON','-DBoost_USE_RELEASE_LIBS=OFF'))
Run 'pinocchio-build' 'cmake.exe' @('--build',"$taskRoot/pinocchio-build",'--config','Debug','--target','install',"-j$Jobs")
Run 'plugin-config' 'cmake.exe' (@('-S',"$repoRoot/src/iCAX-Plugins/geometry/MachineKinematicsServices",'-B',"$taskRoot/plugin-build")+$common+@("-DICAX_BINARY_DIRECTORY=$FrameworkBinaryDirectory",'-DBoost_USE_STATIC_LIBS=ON','-DBoost_USE_DEBUG_LIBS=ON','-DBoost_USE_RELEASE_LIBS=OFF'))
Run 'plugin-build' 'cmake.exe' @('--build',"$taskRoot/plugin-build",'--config','Debug',"-j$Jobs")
if(!$SkipTests){Run 'native-tests' 'ctest.exe' @('--test-dir',"$taskRoot/plugin-build",'-C','Debug','--output-on-failure')}
Run 'plugin-install' 'cmake.exe' @('--install',"$taskRoot/plugin-build",'--config','Debug')
$licenseDirectory=Join-Path $install 'licenses/MachineKinematicsServices'
New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
Copy-Item -LiteralPath "$taskRoot/pinocchio-2.7.0/COPYING.LESSER" -Destination "$licenseDirectory/Pinocchio.txt"
Copy-Item -LiteralPath "$taskRoot/ceres-solver-2.1.0/LICENSE" -Destination "$licenseDirectory/Ceres.txt"
Copy-Item -LiteralPath "$taskRoot/boost_1_87_0/LICENSE_1_0.txt" -Destination "$licenseDirectory/Boost.txt"
Copy-Item -LiteralPath "$taskRoot/eigen-3.4.0/COPYING.MPL2" -Destination "$licenseDirectory/Eigen.txt"
@{configuration='Debug';runtime='MDd';pinocchio='2.7.0';ceres='2.1.0';eigen='3.4.0';boost='1.87.0';python=$false;ros=$false;sourceRoot=$taskRoot;plugin="$install/bin/MachineKinematicsServices.dll";testsRun=(!$SkipTests)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $install 'MachineKinematicsServices.build.json') -Encoding utf8
Write-Host "Built: $install/bin/MachineKinematicsServices.dll (official Pinocchio algorithms and static Ceres compiled into plugin; no Python/ROS runtime)."
