param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build/x64'
$install=Join-Path $PSScriptRoot "install/x64/$Configuration"
cmake -S $PSScriptRoot -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_INSTALL_PREFIX=$install"
if($LASTEXITCODE){throw 'KDL configure failed'}
cmake --build $build --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'KDL build failed'}
cmake --install $build --config $Configuration --prefix $install
if($LASTEXITCODE){throw 'KDL install failed'}
