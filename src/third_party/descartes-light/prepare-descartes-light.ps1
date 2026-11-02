param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[int]$Parallel=4)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$downloads=@(
    @('source-49e2ee3.zip','https://codeload.github.com/swri-robotics/descartes_light/zip/49e2ee3305b5d7ba5018f28cf706e0dc71ecf6cf','958F9C37719741415D9970354AE463C38112E5EAAEC67A52F7B58859CA16B192','descartes_light-49e2ee3305b5d7ba5018f28cf706e0dc71ecf6cf'),
    @('console-bridge-1.0.2.zip','https://codeload.github.com/ros/console_bridge/zip/refs/tags/1.0.2','3B09ADD927FD9CC63D8A8CC53FBD7F5B6C8B6142A155A5DB0BF2E48829F1D4D9','console_bridge-1.0.2')
)
foreach($item in $downloads){
    $archive=Join-Path $root $item[0]
    if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $item[1] -OutFile $archive -TimeoutSec 120}
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $item[2]){throw "Source checksum mismatch: $archive"}
    if(!(Test-Path -LiteralPath (Join-Path $root $item[3]))){Expand-Archive -LiteralPath $archive -DestinationPath $root}
}
if(!(Test-Path -LiteralPath (Join-Path $root '../coal/eigen-3.4.0/Eigen/Core'))){throw 'Prepare Coal/Eigen dependencies first'}
$build=Join-Path $root 'build/vs2022-x64'
$install=Join-Path $root "install/x64/$Configuration"
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2 /D_DEBUG'
if($LASTEXITCODE){throw 'Descartes configure failed'}
& cmake --build $build --config $Configuration --parallel $Parallel
if($LASTEXITCODE){throw 'Descartes build failed'}
& cmake --install $build --config $Configuration --prefix $install
if($LASTEXITCODE){throw 'Descartes install failed'}
$notices=Join-Path $install 'licenses'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'descartes_light-49e2ee3305b5d7ba5018f28cf706e0dc71ecf6cf/LICENSE.Apache-2.0') -Destination (Join-Path $notices 'Descartes-Light-Apache-2.0.txt')
Copy-Item -LiteralPath (Join-Path $root 'console_bridge-1.0.2/LICENSE') -Destination (Join-Path $notices 'Console-Bridge-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root '../coal/eigen-3.4.0/COPYING.MPL2') -Destination (Join-Path $notices 'Eigen-MPL2.txt')
Copy-Item -LiteralPath (Join-Path $root '../coal/eigen-3.4.0/COPYING.BSD') -Destination (Join-Path $notices 'Eigen-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root 'SOURCES.txt') -Destination (Join-Path $notices 'THIRD_PARTY_SOURCES.txt')
