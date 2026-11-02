param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[int]$Parallel=4)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$downloads=Join-Path $root 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$sources=@(
 @{Name='coal-3.0.4';Archive='coal-3.0.4.tar.gz';Url='https://codeload.github.com/coal-library/coal/tar.gz/refs/tags/v3.0.4';Sha='0A4F58E55B88A3D9F873CE79979AECC08491D212EA0B9311DF013DF3B22BFFAC'},
 @{Name='eigen-3.4.0';Archive='eigen-3.4.0.tar.gz';Url='https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz';Sha='8586084F71F9BDE545EE7FA6D00288B264A2B7AC3607B974E54D13E7162C1C72'},
 @{Name='boost_1_87_0';Archive='boost_1_87_0.tar.gz';Url='https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz';Sha='F55C340AA49763B1925CCF02B2E83F35FDCF634C9D5164A2ACB87540173C741D'}
)
foreach($source in $sources){
 $archive=Join-Path $downloads $source.Archive
 if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $source.Url -OutFile $archive -TimeoutSec 600}
 if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $source.Sha){throw "Source checksum mismatch: $archive"}
 if(!(Test-Path -LiteralPath (Join-Path $root $source.Name))){& tar -xzf $archive -C $root;if($LASTEXITCODE){throw "Extraction failed: $archive"}}
}
$build=Join-Path $root 'build/vs2022-x64'
$install=Join-Path $root "install/x64/$Configuration"
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2 /D_DEBUG'
if($LASTEXITCODE){throw 'Coal configure failed'}
& cmake --build $build --config $Configuration --parallel $Parallel
if($LASTEXITCODE){throw 'Coal build failed'}
& cmake --install $build --config $Configuration --prefix $install
if($LASTEXITCODE){throw 'Coal installation failed'}
$licenses=Join-Path $install 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'coal-3.0.4/LICENSE') -Destination (Join-Path $licenses 'Coal-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root 'boost_1_87_0/LICENSE_1_0.txt') -Destination (Join-Path $licenses 'Boost.txt')
Copy-Item -LiteralPath (Join-Path $root 'eigen-3.4.0/COPYING.MPL2') -Destination (Join-Path $licenses 'Eigen-MPL2.txt')
Copy-Item -LiteralPath (Join-Path $root 'eigen-3.4.0/COPYING.BSD') -Destination (Join-Path $licenses 'Eigen-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root 'SOURCES.txt') -Destination (Join-Path $licenses 'THIRD_PARTY_SOURCES.txt')
