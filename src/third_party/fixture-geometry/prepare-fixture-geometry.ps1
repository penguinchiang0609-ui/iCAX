param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[int]$Parallel=8)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$downloads=Join-Path $root 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
$sources=@(
 @{Name='VTK-9.5.2';Archive='vtk-9.5.2.tar.gz';Url='https://codeload.github.com/Kitware/VTK/tar.gz/refs/tags/v9.5.2';Sha='09FF15D8C453BF090A27C13251B73F5CB4EA2418D2EAC44E7ECA144D15472E4D'},
 @{Name='Clipper2-Clipper2_1.5.4';Archive='clipper2-1.5.4.tar.gz';Url='https://codeload.github.com/AngusJohnson/Clipper2/tar.gz/refs/tags/Clipper2_1.5.4';Sha='9D8A35A29D04CD1B7B45F542C0BA48015FEECE1210036EA9E4EFAAD3140AF4B0'}
)
foreach($source in $sources){
 $archive=Join-Path $downloads $source.Archive
 if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest $source.Url -OutFile $archive}
 if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $source.Sha){throw "Source checksum mismatch: $archive"}
 if(!(Test-Path -LiteralPath (Join-Path $root $source.Name))){& tar -xzf $archive -C $root;if($LASTEXITCODE){throw 'Extraction failed'}}
}
$build=Join-Path $root 'build/vs2022-x64'
$install=Join-Path $root "install/x64/$Configuration"
cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2 /D_DEBUG' '-DCMAKE_C_FLAGS_DEBUG=/MDd /Zi /O2 /Ob2 /D_DEBUG'
if($LASTEXITCODE){throw 'Fixture geometry configure failed'}
$env:CL="/MP$Parallel"
cmake --build $build --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'Fixture geometry build failed'}
cmake --install $build --config $Configuration --prefix $install
if($LASTEXITCODE){throw 'Fixture geometry install failed'}
$licenses=Join-Path $install 'licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'VTK-9.5.2/Copyright.txt') -Destination (Join-Path $licenses 'VTK-BSD.txt')
Copy-Item -LiteralPath (Join-Path $root 'Clipper2-Clipper2_1.5.4/LICENSE') -Destination (Join-Path $licenses 'Clipper2-BSL.txt')
# VTK includes third-party components. Preserve the installed notice set.
Get-ChildItem -LiteralPath (Join-Path $install 'share/licenses') -Recurse -File | ForEach-Object {
 Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenses ($_.Directory.Name+'-'+$_.Name))
}
