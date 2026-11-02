param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[int]$Parallel=4)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$archive=Join-Path $root 'downloads/ompl-2.0.2.tar.gz'
New-Item -ItemType Directory -Path (Split-Path $archive) -Force | Out-Null
if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri 'https://codeload.github.com/ompl/ompl/tar.gz/refs/tags/2.0.2' -OutFile $archive -TimeoutSec 300}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'D867190A46A7F730923475A68AF2E09FC9E796017A59BE395A4BF3900D0C3AE4'){throw 'OMPL source checksum mismatch'}
if(!(Test-Path -LiteralPath (Join-Path $root 'ompl-2.0.2'))){& tar -xzf $archive -C $root;if($LASTEXITCODE){throw 'OMPL extraction failed'}}
if(!(Test-Path -LiteralPath (Join-Path $root '../coal/boost_1_87_0/boost/version.hpp'))){throw 'Prepare Coal dependencies first'}
$build=Join-Path $root 'build/vs2022-x64'
$install=Join-Path $root "install/x64/$Configuration"
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2 /D_DEBUG'
if($LASTEXITCODE){throw 'OMPL configure failed'}
& cmake --build $build --config $Configuration --parallel $Parallel
if($LASTEXITCODE){throw 'OMPL build failed'}
& cmake --install $build --config $Configuration --prefix $install
if($LASTEXITCODE){throw 'OMPL install failed'}
$notices=Join-Path $install 'licenses'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'ompl-2.0.2/LICENSE') -Destination (Join-Path $notices 'OMPL-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root '../coal/boost_1_87_0/LICENSE_1_0.txt') -Destination (Join-Path $notices 'Boost.txt')
Copy-Item -LiteralPath (Join-Path $root '../coal/eigen-3.4.0/COPYING.MPL2') -Destination (Join-Path $notices 'Eigen-MPL2.txt')
Copy-Item -LiteralPath (Join-Path $root '../coal/eigen-3.4.0/COPYING.BSD') -Destination (Join-Path $notices 'Eigen-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root 'SOURCES.txt') -Destination (Join-Path $notices 'THIRD_PARTY_SOURCES.txt')
